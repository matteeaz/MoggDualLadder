#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "PluginProcessor.h"

class MoggLookAndFeel : public juce::LookAndFeel_V4 {
public:
    MoggLookAndFeel();

    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawLabel(juce::Graphics& g, juce::Label& label) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;
};

class MoggEditorContent : public juce::Component,
                          private juce::Timer {
public:
    explicit MoggEditorContent(MoggDualLadderAudioProcessor&);
    ~MoggEditorContent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    MoggDualLadderAudioProcessor& audioProcessor;

    juce::Slider inGainSlider, outGainSlider;
    juce::Slider hpFreqSlider, hpResSlider, hpSatSlider;
    juce::Slider dirtSlider, biasSlider;
    juce::Slider lpFreqSlider, lpResSlider, lpSatSlider;

    juce::ComboBox hpSatModeBox, lpSatModeBox;
    juce::ToggleButton linkButton     { "LINK FREQ" };
    juce::ToggleButton airBoostButton { "AIR BOOST" };

    std::array<juce::TextButton, 4> slopeButtons {
        juce::TextButton("6 dB"),
        juce::TextButton("12 dB"),
        juce::TextButton("24 dB"),
        juce::TextButton("48 dB")
    };

    juce::Label inGainLabel, outGainLabel;
    juce::Label hpResLabel, hpSatAmtLabel, hpSatModeLabel;
    juce::Label dirtLabel, biasLabel, airBoostLabel;
    juce::Label lpResLabel, lpSatAmtLabel, lpSatModeLabel;

    juce::Rectangle<int> inputModuleRect, hpModuleRect, modModuleRect, lpModuleRect, outputModuleRect;
    juce::Rectangle<int> inputMeterRect, outputMeterRect;
    juce::Rectangle<int> hpSatBoxRect, lpSatBoxRect;

    float dispInL  = 0.0f, dispInR  = 0.0f;
    float dispOutL = 0.0f, dispOutR = 0.0f;

    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment> inGainAttachment, outGainAttachment;
    std::unique_ptr<SliderAttachment> hpFreqAttachment, hpResAttachment, hpSatAttachment;
    std::unique_ptr<SliderAttachment> dirtAttachment, biasAttachment;
    std::unique_ptr<SliderAttachment> lpFreqAttachment, lpResAttachment, lpSatAttachment;
    std::unique_ptr<ComboBoxAttachment> hpSatModeAttachment, lpSatModeAttachment;
    std::unique_ptr<ButtonAttachment> linkAttachment, airBoostAttachment;
    std::unique_ptr<juce::ParameterAttachment> slopeAttachment;

    void setupRotarySlider(juce::Slider& slider, juce::Label* label, const juce::String& text,
                           juce::Colour accent, int knobRole,
                           juce::Slider::TextEntryBoxPosition textBoxPos = juce::Slider::TextBoxBelow,
                           int textW = 76, int textH = 18);
    void setupSatModeBox(juce::ComboBox& box);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MoggEditorContent)
};

class MoggDualLadderAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit MoggDualLadderAudioProcessorEditor(MoggDualLadderAudioProcessor&);
    ~MoggDualLadderAudioProcessorEditor() override;

    void resized() override;

private:
    static constexpr int kBaseWidth  = 1080;
    static constexpr int kBaseHeight = 468;

    MoggLookAndFeel moggLookAndFeel;
    MoggEditorContent content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MoggDualLadderAudioProcessorEditor)
};
