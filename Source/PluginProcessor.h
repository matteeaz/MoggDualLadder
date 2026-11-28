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

    // Sauberes Starved-Capacitor Envelope-Choking (ohne zusätzliche Verzerrung)
    struct StarvedBiasStage {
        float fastEnv          = 0.0f;
        float peakRefEnv       = 0.0f;
        float capEnergy        = 1.0f;
        float sputterEnv       = 0.0f;
        float gateGain         = 1.0f;
        bool  gateOpen         = false;
        bool  capStarved       = false;

        float osSampleRate     = 176400.0f;
        float fastAttackCoeff  = 0.02f;
        float fastReleaseCoeff = 0.001f;
        float peakAttackCoeff  = 0.01f;
        float peakReleaseCoeff = 0.0001f;
        float gateOpenCoeff    = 0.01f;
        float gateCloseCoeff   = 0.005f;
        float sputterCoeff     = 0.003f;
        uint32_t rngState      = 0x12345678u;

        void prepare(double oversampledRate, uint32_t seed) noexcept {
            rngState = seed;
            if (oversampledRate > 0.0) {
                osSampleRate     = static_cast<float>(oversampledRate);
                fastAttackCoeff  = 1.0f - std::exp(-1.0f / (0.0005f * osSampleRate));
                fastReleaseCoeff = 1.0f - std::exp(-1.0f / (0.0080f * osSampleRate));
                peakAttackCoeff  = 1.0f - std::exp(-1.0f / (0.0020f * osSampleRate));
                peakReleaseCoeff = 1.0f - std::exp(-1.0f / (0.3500f * osSampleRate));
                gateOpenCoeff    = 1.0f - std::exp(-1.0f / (0.0008f * osSampleRate));
                gateCloseCoeff   = 1.0f - std::exp(-1.0f / (0.0018f * osSampleRate));
                sputterCoeff     = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 90.0f / osSampleRate);
            }
            reset();
        }

        void reset() noexcept {
            fastEnv    = 0.0f;
            peakRefEnv = 0.0f;
            capEnergy  = 1.0f;
            sputterEnv = 0.0f;
            gateGain   = 1.0f;
            gateOpen   = false;
            capStarved = false;
        }

        float processSample(float input, float u, float satNorm, float dirtNorm) noexcept {
            const float absIn = std::abs(input);

            // 1. Glatte Signal-Hüllkurve (verhindert Audio-Rate-Chopping / Verzerrung)
            const float fCoeff = (absIn > fastEnv) ? fastAttackCoeff : fastReleaseCoeff;
            fastEnv += fCoeff * (absIn - fastEnv);

            // 2. Lokale Pegelreferenz (damit leise & laute Signale gleichmäßig choken)
            const float pCoeff = (absIn > peakRefEnv) ? peakAttackCoeff : peakReleaseCoeff;
            peakRefEnv += pCoeff * (absIn - peakRefEnv);
            const float levelScale = std::max(0.02f, peakRefEnv);

            // 3. Dirt-gekoppelte Versorgungsschwankungen (nur Hüllkurven-Modulation)
            float dirtDip = 0.0f;
            if (dirtNorm > 0.0001f) {
                rngState = rngState * 1664525u + 1013904223u;
                const float white = (static_cast<float>(rngState >> 8) / 8388608.0f) - 1.0f;
                const float spike = (std::abs(white) > 0.86f) ? (std::abs(white) * 2.2f) : (white * 0.20f);
                sputterEnv += sputterCoeff * (spike - sputterEnv);
                dirtDip = dirtNorm * std::pow(u, 1.15f) * std::max(0.0f, sputterEnv);
            } else {
                sputterEnv = 0.0f;
            }

            // 4. Schwellwert-Choke (schneidet Tails, Sustain & leise Passagen ab)
            const float chokeRatio  = std::min(0.92f, 0.88f * std::pow(u, 1.10f) + 0.35f * dirtDip);
            const float openThresh  = levelScale * chokeRatio;
            const float closeThresh = openThresh * 0.78f;

            if (gateOpen) {
                if (fastEnv < closeThresh) gateOpen = false;
            } else {
                if (fastEnv > openThresh) gateOpen = true;
            }

            // 5. Starved Capacitor (entlädt sich während eines Tons und würgt ihn ab)
            const float capStrength = juce::jlimit(0.0f, 1.0f, (u - 0.25f) / 0.75f);
            if (capStrength > 0.001f) {
                const float drainSec    = juce::jmap(capStrength, 0.140f, 0.016f) / (1.0f + 0.55f * satNorm + 0.65f * dirtDip);
                const float rechargeSec = juce::jmap(capStrength, 0.020f, 0.055f);

                if (gateOpen && !capStarved) {
                    const float drainStep = 1.0f / std::max(1.0f, drainSec * osSampleRate);
                    capEnergy = std::max(0.0f, capEnergy - drainStep);
                    if (capEnergy <= 0.22f) capStarved = true;
                } else {
                    const float rechargeStep = 1.0f / std::max(1.0f, rechargeSec * osSampleRate);
                    capEnergy = std::min(1.0f, capEnergy + rechargeStep);
                    if (capEnergy >= 0.44f) capStarved = false;
                }
            } else {
                capEnergy  = 1.0f;
                capStarved = false;
            }

            // 6. Sauberes VCA-Gating (null zusätzliche harmonische Verzerrung)
            const bool shouldPass = gateOpen && !capStarved;
            const float targetGain = shouldPass ? 1.0f : 0.0f;
            const float gCoeff = (targetGain > gateGain) ? gateOpenCoeff : gateCloseCoeff;
            gateGain += gCoeff * (targetGain - gateGain);

            return input * gateGain;
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
