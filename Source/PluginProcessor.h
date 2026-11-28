#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
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
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static void storeMaxPeak(std::atomic<float>& target, float peak) noexcept {
        float current = target.load(std::memory_order_relaxed);
        while (peak > current && !target.compare_exchange_weak(current, peak, std::memory_order_relaxed)) {}
    }

    MoggDualLadderFilter filterL;
    MoggDualLadderFilter filterR;

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
