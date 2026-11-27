#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "PluginProcessor.h"

class MoggDualLadderAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit MoggDualLadderAudioProcessorEditor(MoggDualLadderAudioProcessor&);
    ~MoggDualLadderAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    MoggDualLadderAudioProcessor& audioProcessor;

    juce::Slider inGainSlider, outGainSlider;
    juce::Slider hpFreqSlider, hpResSlider;
    juce::Slider lpFreqSlider, lpResSlider;
    juce::ToggleButton linkButton { "LINK FREQ" };

    std::array<juce::TextButton, 4> slopeButtons {
        juce::TextButton("6 dB"),
        juce::TextButton("12 dB"),
        juce::TextButton("24 dB"),
        juce::TextButton("48 dB")
    };

    juce::Label inGainLabel, outGainLabel;
    juce::Label hpFreqLabel, hpResLabel, lpFreqLabel, lpResLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> inGainAttachment, outGainAttachment;
    std::unique_ptr<SliderAttachment> hpFreqAttachment, hpResAttachment;
    std::unique_ptr<SliderAttachment> lpFreqAttachment, lpResAttachment;
    std::unique_ptr<ButtonAttachment> linkAttachment;
    std::unique_ptr<juce::ParameterAttachment> slopeAttachment;

    void setupRotarySlider(juce::Slider& slider, juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MoggDualLadderAudioProcessorEditor)
};
