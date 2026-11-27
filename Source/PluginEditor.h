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
    juce::Slider hpFreqSlider, hpResSlider, hpSatSlider;
    juce::Slider screamSlider;
    juce::Slider lpFreqSlider, lpResSlider, lpSatSlider;

    juce::ComboBox hpSatModeBox, lpSatModeBox;
    juce::ToggleButton linkButton { "LINK FREQ" };

    std::array<juce::TextButton, 4> slopeButtons {
        juce::TextButton("6 dB"),
        juce::TextButton("12 dB"),
        juce::TextButton("24 dB"),
        juce::TextButton("48 dB")
    };

    juce::Label inGainLabel, outGainLabel;
    juce::Label hpFreqLabel, hpResLabel, hpSatLabel;
    juce::Label screamLabel;
    juce::Label lpFreqLabel, lpResLabel, lpSatLabel;

    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment> inGainAttachment, outGainAttachment;
    std::unique_ptr<SliderAttachment> hpFreqAttachment, hpResAttachment, hpSatAttachment;
    std::unique_ptr<SliderAttachment> screamAttachment;
    std::unique_ptr<SliderAttachment> lpFreqAttachment, lpResAttachment, lpSatAttachment;
    std::unique_ptr<ComboBoxAttachment> hpSatModeAttachment, lpSatModeAttachment;
    std::unique_ptr<ButtonAttachment> linkAttachment;
    std::unique_ptr<juce::ParameterAttachment> slopeAttachment;

    void setupRotarySlider(juce::Slider& slider, juce::Label& label, const juce::String& text, juce::Colour accent);
    void setupSatModeBox(juce::ComboBox& box);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MoggDualLadderAudioProcessorEditor)
};
