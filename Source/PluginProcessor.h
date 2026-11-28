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

    // Aggressiv abgehackte Starved-Bias Stufe (4x-Oversampling, gekoppelt mit Saturation & Dirt)
    struct StarvedBiasStage {
        float peakRefEnv       = 0.0f;
        float sagEnv           = 0.0f;
        float sputterEnv       = 0.0f;
        float gateGain         = 0.0f;
        bool  gateOpen         = false;
        float dcPrevIn         = 0.0f;
        float dcPrevOut        = 0.0f;

        float peakAttackCoeff  = 0.01f;
        float peakReleaseCoeff = 0.0001f;
        float sagAttackCoeff   = 0.01f;
        float sagReleaseCoeff  = 0.0004f;
        float gateSmoothCoeff  = 0.05f;
        float sputterCoeff     = 0.004f;
        float dcBlockerCoeff   = 0.9995f;
        uint32_t rngState      = 0x12345678u;

        void prepare(double oversampledRate, uint32_t seed) noexcept {
            rngState = seed;
            if (oversampledRate > 0.0) {
                const float fs = static_cast<float>(oversampledRate);
                peakAttackCoeff  = 1.0f - std::exp(-1.0f / (0.0020f * fs));
                peakReleaseCoeff = 1.0f - std::exp(-1.0f / (0.3200f * fs));
                sagAttackCoeff   = 1.0f - std::exp(-1.0f / (0.0012f * fs));
                sagReleaseCoeff  = 1.0f - std::exp(-1.0f / (0.0650f * fs));
                gateSmoothCoeff  = 1.0f - std::exp(-1.0f / (0.00035f * fs));
                sputterCoeff     = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 115.0f / fs);
                dcBlockerCoeff   = 1.0f - (juce::MathConstants<float>::twoPi * 14.0f / fs);
            }
            reset();
        }

        void reset() noexcept {
            peakRefEnv = 0.0f;
            sagEnv     = 0.0f;
            sputterEnv = 0.0f;
            gateGain   = 0.0f;
            gateOpen   = false;
            dcPrevIn   = 0.0f;
            dcPrevOut  = 0.0f;
        }

        float processSample(float input, float u, float satNorm, float dirtNorm) noexcept {
            const float absIn = std::abs(input);

            // 1. Lokale Pegelreferenz (damit auch leise Signale sauber zerhackt werden)
            const float pCoeff = (absIn > peakRefEnv) ? peakAttackCoeff : peakReleaseCoeff;
            peakRefEnv += pCoeff * (absIn - peakRefEnv);
            const float levelScale = std::max(0.08f, peakRefEnv);

            // 2. Schneller Voltage-Sag / Capacitor-Choke (lässt den Ton nach Transienten ersticken)
            const float sCoeff = (absIn > sagEnv) ? sagAttackCoeff : sagReleaseCoeff;
            sagEnv += sCoeff * (absIn - sagEnv);

            // 3. Dirt-Sputter (unregelmäßige Batterie-Aussetzer bei aufgedrehtem DIRT)
            float dirtMod = 0.0f;
            if (dirtNorm > 0.0001f) {
                rngState = rngState * 1664525u + 1013904223u;
                const float white = (static_cast<float>(rngState >> 8) / 8388608.0f) - 1.0f;
                const float spike = (std::abs(white) > 0.84f) ? (std::abs(white) * 2.6f) : (white * 0.25f);
                sputterEnv += sputterCoeff * (spike - sputterEnv);
                const float dirtJitter = levelScale * 0.65f * std::pow(u, 1.10f);
                dirtMod = dirtJitter * dirtNorm * std::max(0.0f, sputterEnv);
            } else {
                sputterEnv = 0.0f;
            }

            // 4. Asymmetrische Transistor-Sperrschicht (negative Halbwelle verhungert zuerst)
            const float basePosThresh = levelScale * (0.48f * std::pow(u, 1.15f));
            const float sagDepth      = levelScale * (0.68f * std::pow(u, 1.10f));
            const float dynamicSag    = sagDepth * (0.45f + 0.55f * satNorm) * std::min(sagEnv / levelScale, 1.6f);

            const float threshPosOpen  = basePosThresh + dynamicSag + dirtMod;
            const float threshNegOpen  = threshPosOpen + levelScale * (0.88f * std::pow(u, 1.05f));

            const float activeThreshOpen  = (input >= 0.0f) ? threshPosOpen : threshNegOpen;
            const float activeThreshClose = activeThreshOpen * 0.70f;

            // 5. Snap-Gate mit Hysterese (abruptes Auf- und Zuschnappen)
            if (gateOpen) {
                if (absIn < activeThreshClose) gateOpen = false;
            } else {
                if (absIn > activeThreshOpen) gateOpen = true;
            }

            const float targetGate = gateOpen ? 1.0f : 0.0f;
            gateGain += gateSmoothCoeff * (targetGate - gateGain);

            float chopped = 0.0f;
            if (absIn > activeThreshClose) {
                const float excess = absIn - activeThreshClose;
                chopped = (input >= 0.0f) ? excess : -excess;
            }
            float gated = chopped * gateGain;

            // 6. Starved Rail Sättigung & Makeup-Gain für druckvolle abgehackte Impulse
            const float railCeiling = 1.35f - 0.88f * std::pow(u, 1.10f);
            if (gated > 0.0f) {
                gated = railCeiling * std::tanh(gated / railCeiling);
            }

            const float makeupGain = 1.0f + 1.15f * std::pow(u, 1.10f);
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
