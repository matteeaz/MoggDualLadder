#include "PluginProcessor.h"
#include "PluginEditor.h"

MoggDualLadderAudioProcessorEditor::MoggDualLadderAudioProcessorEditor(MoggDualLadderAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setupRotarySlider(inGainSlider,  inGainLabel,  "IN LEVEL");
    setupRotarySlider(hpFreqSlider,  hpFreqLabel,  "HP CUTOFF");
    setupRotarySlider(hpResSlider,   hpResLabel,   "HP RES");
    setupRotarySlider(lpFreqSlider,  lpFreqLabel,  "LP CUTOFF");
    setupRotarySlider(lpResSlider,   lpResLabel,   "LP RES");
    setupRotarySlider(outGainSlider, outGainLabel, "OUT LEVEL");

    linkButton.setColour(juce::ToggleButton::textColourId, juce::Colours::orange);
    linkButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::orange);
    addAndMakeVisible(linkButton);

    for (size_t i = 0; i < slopeButtons.size(); ++i) {
        auto& btn = slopeButtons[i];
        btn.setRadioGroupId(1001);
        btn.setClickingTogglesState(true);
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff18191c));
        btn.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
        btn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff18191c));

        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < slopeButtons.size() - 1) edges |= juce::Button::ConnectedOnRight;
        btn.setConnectedEdges(edges);

        btn.onClick = [this, i]() {
            if (slopeButtons[i].getToggleState() && slopeAttachment != nullptr) {
                slopeAttachment->setValueAsCompleteGesture(static_cast<float>(i));
            }
        };

        addAndMakeVisible(btn);
    }

    if (auto* slopeParam = audioProcessor.apvts.getParameter("slope")) {
        slopeAttachment = std::make_unique<juce::ParameterAttachment>(
            *slopeParam,
            [this](float newValue) {
                const int idx = juce::jlimit(0, 3, juce::roundToInt(newValue));
                slopeButtons[static_cast<size_t>(idx)].setToggleState(true, juce::dontSendNotification);
            },
            nullptr
        );
        slopeAttachment->sendInitialUpdate();
    }

    inGainAttachment  = std::make_unique<SliderAttachment>(audioProcessor.apvts, "inGain",  inGainSlider);
    hpFreqAttachment  = std::make_unique<SliderAttachment>(audioProcessor.apvts, "hpFreq",  hpFreqSlider);
    hpResAttachment   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "hpRes",   hpResSlider);
    lpFreqAttachment  = std::make_unique<SliderAttachment>(audioProcessor.apvts, "lpFreq",  lpFreqSlider);
    lpResAttachment   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "lpRes",   lpResSlider);
    outGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "outGain", outGainSlider);
    linkAttachment    = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "link",    linkButton);

    setSize(760, 260);
}

void MoggDualLadderAudioProcessorEditor::setupRotarySlider(juce::Slider& slider, juce::Label& label, const juce::String& text) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 20);
    slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::orange);
    slider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(label);
}

void MoggDualLadderAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff18191c));

    g.setColour(juce::Colour(0xff25272c));
    g.fillRoundedRectangle(12.0f, 12.0f, getWidth() - 24.0f, getHeight() - 24.0f, 10.0f);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("MOGG DUAL LADDER", 24, 20, 240, 26, juce::Justification::left);
}

void MoggDualLadderAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(24);
    auto topBar = area.removeFromTop(28);

    linkButton.setBounds(topBar.removeFromRight(115));

    const int switchTotalWidth = 216;
    const int btnWidth = switchTotalWidth / 4;
    auto switchBounds = juce::Rectangle<int>(
        (getWidth() - switchTotalWidth) / 2 + 15,
        topBar.getY() + 1,
        switchTotalWidth,
        24
    );

    for (size_t i = 0; i < slopeButtons.size(); ++i) {
        slopeButtons[i].setBounds(switchBounds.removeFromLeft(btnWidth));
    }

    area.removeFromTop(12);
    const int colWidth = area.getWidth() / 6;

    auto placeControl = [&](juce::Slider& s, juce::Label& l) {
        auto col = area.removeFromLeft(colWidth).reduced(5);
        l.setBounds(col.removeFromTop(20));
        s.setBounds(col);
    };

    placeControl(inGainSlider,  inGainLabel);
    placeControl(hpFreqSlider,  hpFreqLabel);
    placeControl(hpResSlider,   hpResLabel);
    placeControl(lpFreqSlider,  lpFreqLabel);
    placeControl(lpResSlider,   lpResLabel);
    placeControl(outGainSlider, outGainLabel);
}
