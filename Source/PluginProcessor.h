#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include "MoggDualLadderFilter.h"

class MoggDualLadderAudioProcessor : public juce::AudioProcessor,
                                     public juce::AudioProcessorValueTreeState::Listener {
public:
    MoggDualLadderAudioProcessor();
    ~MoggDualLadderAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void parameterChanged(const juce::String& parameterID, float newValue) override;

    float consumeInputPeakL() noexcept  { return inPeakL.exchange(0.0f, std::memory_order_relaxed); }
    float consumeInputPeakR() noexcept  { return inPeakR.exchange(0.0f, std::memory_order_relaxed); }
    float consumeOutputPeakL() noexcept { return outPeakL.exchange(0.0f, std::memory_order_relaxed); }
    float consumeOutputPeakR() noexcept { return outPeakR.exchange(0.0f, std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct HighShelfBiquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void reset() noexcept {
            z1 = 0.0;
            z2 = 0.0;
        }

        void setCoefficients(double sampleRate, double freqHz, double slopeS, double gainDb) noexcept {
            if (sampleRate <= 0.0 || std::abs(gainDb) < 1.0e-4) {
                b0 = 1.0; b1 = 0.0; b2 = 0.0; a1 = 0.0; a2 = 0.0;
                return;
            }

            const double A     = std::pow(10.0, gainDb / 40.0);
            const double w0    = juce::MathConstants<double>::twoPi * std::min(freqHz, sampleRate * 0.45) / sampleRate;
            const double cosW0 = std::cos(w0);
            const double sinW0 = std::sin(w0);
            const double alpha = (sinW0 * 0.5) * std::sqrt((A + 1.0 / A) * (1.0 / slopeS - 1.0) + 2.0);
            const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;

            const double a0    = (A + 1.0) - (A - 1.0) * cosW0 + twoSqrtAAlpha;
            const double invA0 = 1.0 / a0;

            b0 = (A * ((A + 1.0) + (A - 1.0) * cosW0 + twoSqrtAAlpha)) * invA0;
            b1 = (-2.0 * A * ((A - 1.0) + (A + 1.0) * cosW0))          * invA0;
            b2 = (A * ((A + 1.0) + (A - 1.0) * cosW0 - twoSqrtAAlpha)) * invA0;
            a1 = (2.0 * ((A - 1.0) - (A + 1.0) * cosW0))               * invA0;
            a2 = ((A + 1.0) - (A - 1.0) * cosW0 - twoSqrtAAlpha)       * invA0;
        }

        float processSample(float input) noexcept {
            const double x = static_cast<double>(input);
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return static_cast<float>(y);
        }
    };

    // Starved-Bias Stufe (läuft im 4x-Oversampling-Pfad, gekoppelt mit Saturation & Dirt)
    struct StarvedBiasStage {
        float sagEnv          = 0.0f;
        float sputterEnv      = 0.0f;
        float dcPrevIn        = 0.0f;
        float dcPrevOut       = 0.0f;
        float sagAttackCoeff  = 0.01f;
        float sagReleaseCoeff = 0.0005f;
        float sputterCoeff    = 0.006f;
        float dcBlockerCoeff  = 0.9997f;
        uint32_t rngState     = 0x12345678u;

        void prepare(double oversampledRate, uint32_t seed) noexcept {
            rngState = seed;
            if (oversampledRate > 0.0) {
                sagAttackCoeff  = 1.0f - std::exp(-1.0f / (0.0015f * static_cast<float>(oversampledRate)));
                sagReleaseCoeff = 1.0f - std::exp(-1.0f / (0.0450f * static_cast<float>(oversampledRate)));
                sputterCoeff    = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 180.0f / static_cast<float>(oversampledRate));
                dcBlockerCoeff  = 1.0f - (juce::MathConstants<float>::twoPi * 6.0f / static_cast<float>(oversampledRate));
            }
            reset();
        }

        void reset() noexcept {
            sagEnv     = 0.0f;
            sputterEnv = 0.0f;
            dcPrevIn   = 0.0f;
            dcPrevOut  = 0.0f;
        }

        float processSample(float input, float u, float satNorm, float dirtNorm) noexcept {
            // 1. Schneller Envelope-Follower für Voltage Sag (knickt nach lauten Transienten weg)
            const float absIn = std::abs(input);
            const float envCoeff = (absIn > sagEnv) ? sagAttackCoeff : sagReleaseCoeff;
            sagEnv += envCoeff * (absIn - sagEnv);

            // 2. Dirt-Sputter (unregelmäßiges Bias-Stottern bei aufgedrehtem DIRT)
            float dirtMod = 0.0f;
            if (dirtNorm > 0.0001f) {
                rngState = rngState * 1664525u + 1013904223u;
                const float white = (static_cast<float>(rngState >> 8) / 8388608.0f) - 1.0f;
                const float spike = (std::abs(white) > 0.91f) ? (white * 1.85f) : (white * 0.35f);
                sputterEnv += sputterCoeff * (spike - sputterEnv);
                const float dirtJitter = 0.15f * std::pow(u, 1.35f);
                dirtMod = dirtJitter * dirtNorm * sputterEnv;
            } else {
                sputterEnv = 0.0f;
            }

            // 3. Skalierungskurven für u in [0, 1] (entspricht Knob 1.0 .. 11.0)
            const float dcBias      = 0.34f * std::pow(u, 1.45f);
            const float baseGate    = 0.28f * std::pow(u, 1.75f);
            const float sagDepth    = 0.30f * std::pow(u, 1.35f);
            const float dynamicSag  = sagDepth * (0.35f + 0.65f * satNorm) * std::min(sagEnv * 1.6f, 1.25f);
            const float threshold   = std::max(0.0f, baseGate + dynamicSag + dirtMod);
            const float knee        = threshold * 0.065f + 1.0e-6f;

            // 4. Arbeitspunkt verschieben & unterhalb der Schwelle abhacken (C1-glattes Gating)
            const float shifted = input - dcBias;
            const float mag     = std::abs(shifted);
            float gated = 0.0f;

            if (mag > threshold) {
                const float excess = mag - threshold;
                const float shaped = (excess < knee) ? ((excess * excess) / (2.0f * knee))
                                                     : (excess - 0.5f * knee);
                gated = (shifted >= 0.0f) ? shaped : -shaped;
            }

            // 5. Asymmetrische Begrenzung an der abfallenden Versorgungsschiene
            const float railCeiling = 1.45f - 0.97f * std::pow(u, 1.15f);
            if (gated > 0.0f) {
                gated = railCeiling * std::tanh(gated / railCeiling);
            }

            // 6. DC-Blocker & Pegel-Ausgleich
            const float makeupGain = 1.0f + 0.42f * std::pow(u, 1.20f);
            float dcOut = gated - dcPrevIn + dcBlockerCoeff * dcPrevOut;
            if (std::abs(dcOut) < 1.0e-15f) dcOut = 0.0f;
            dcPrevIn  = gated;
            dcPrevOut = dcOut;

            return dcOut * makeupGain;
        }
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static void storeMaxPeak(std::atomic<float>& target, float peak) noexcept {
        float current = target.load(std::memory_order_relaxed);
        while (peak > current && !target.compare_exchange_weak(current, peak, std::memory_order_relaxed)) {}
    }

    MoggDualLadderFilter filterL;
    MoggDualLadderFilter filterR;

    StarvedBiasStage biasStageL;
    StarvedBiasStage biasStageR;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> biasNormSmoothed;

    HighShelfBiquad airShelfL;
    HighShelfBiquad airShelfR;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> airShelfGainDb;
    double currentSampleRate = 44100.0;

    juce::dsp::Oversampling<float> oversampling {
        2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true
    };

    std::atomic<float>* inGainParam     = nullptr;
    std::atomic<float>* outGainParam    = nullptr;
    std::atomic<float>* lpFreqParam     = nullptr;
    std::atomic<float>* hpFreqParam     = nullptr;
    std::atomic<float>* lpResParam      = nullptr;
    std::atomic<float>* hpResParam      = nullptr;
    std::atomic<float>* hpSatParam      = nullptr;
    std::atomic<float>* hpSatModeParam  = nullptr;
    std::atomic<float>* lpSatParam      = nullptr;
    std::atomic<float>* lpSatModeParam  = nullptr;
    std::atomic<float>* dirtParam       = nullptr;
    std::atomic<float>* biasParam       = nullptr;
    std::atomic<float>* airBoostParam   = nullptr;
    std::atomic<float>* linkParam       = nullptr;
    std::atomic<float>* slopeParam      = nullptr;

    std::atomic<float> linkedFreqRatio { 0.05f };
    std::atomic<bool> isUpdatingLink { false };
    std::atomic<bool> isRestoringState { false };

    std::atomic<float> inPeakL  { 0.0f };
    std::atomic<float> inPeakR  { 0.0f };
    std::atomic<float> outPeakL { 0.0f };
    std::atomic<float> outPeakR { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MoggDualLadderAudioProcessor)
};
