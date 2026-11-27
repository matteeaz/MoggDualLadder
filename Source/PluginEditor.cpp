#include "PluginProcessor.h"
#include "PluginEditor.h"

MoggDualLadderAudioProcessorEditor::MoggDualLadderAudioProcessorEditor(MoggDualLadderAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    const auto mainAccent   = juce::Colours::orange;
    const auto satAccent    = juce::Colour(0xffff5a26);
    const auto screamAccent = juce::Colour(0xffff2e63);

    setupRotarySlider(inGainSlider,  inGainLabel,  "IN LEVEL",  mainAccent);
    setupRotarySlider(hpFreqSlider,  hpFreqLabel,  "HP CUTOFF", mainAccent);
    setupRotarySlider(hpResSlider,   hpResLabel,   "HP RES",    mainAccent);
    setupRotarySlider(hpSatSlider,   hpSatLabel,   "HP SAT",    satAccent);
    setupRotarySlider(screamSlider,  screamLabel,  "SCREAM",    screamAccent);
    setupRotarySlider(lpFreqSlider,  lpFreqLabel,  "LP CUTOFF", mainAccent);
    setupRotarySlider(lpResSlider,   lpResLabel,   "LP RES",    mainAccent);
    setupRotarySlider(lpSatSlider,   lpSatLabel,   "LP SAT",    satAccent);
    setupRotarySlider(outGainSlider, outGainLabel, "OUT LEVEL", mainAccent);

    setupSatModeBox(hpSatModeBox);
    setupSatModeBox(lpSatModeBox);

    linkButton.setColour(juce::ToggleButton::textColourId, mainAccent);
    linkButton.setColour(juce::ToggleButton::tickColourId, mainAccent);
    addAndMakeVisible(linkButton);

    for (size_t i = 0; i < slopeButtons.size(); ++i) {
        auto& btn = slopeButtons[i];
        btn.setRadioGroupId(1001);
        btn.setClickingTogglesState(true);
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff18191c));
        btn.setColour(juce::TextButton::buttonOnColourId, mainAccent);
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

    inGainAttachment    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "inGain",    inGainSlider);
    hpFreqAttachment    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "hpFreq",    hpFreqSlider);
    hpResAttachment     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "hpRes",     hpResSlider);
    hpSatAttachment     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "hpSat",     hpSatSlider);
    hpSatModeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.apvts, "hpSatMode", hpSatModeBox);
    screamAttachment    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "scream",    screamSlider);
    lpFreqAttachment    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "lpFreq",    lpFreqSlider);
    lpResAttachment     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "lpRes",     lpResSlider);
    lpSatAttachment     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "lpSat",     lpSatSlider);
    lpSatModeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.apvts, "lpSatMode", lpSatModeBox);
    outGainAttachment   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "outGain",   outGainSlider);
    linkAttachment      = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "link",      linkButton);

    setSize(1020, 290);
}

void MoggDualLadderAudioProcessorEditor::setupRotarySlider(juce::Slider& slider, juce::Label& label,
                                                           const juce::String& text, juce::Colour accent) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 82, 20);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(label);
}

void MoggDualLadderAudioProcessorEditor::setupSatModeBox(juce::ComboBox& box) {
    box.addItem("LADDER", 1);
    box.addItem("WARM",   2);
    box.addItem("FOLD",   3);
    box.addItem("GRIT",   4);
    box.setJustificationType(juce::Justification::centred);
    box.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff18191c));
    box.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    box.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3a3e47));
    box.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xffff5a26));
    addAndMakeVisible(box);
}

void MoggDualLadderAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff18191c));

    g.setColour(juce::Colour(0xff25272c));
    g.fillRoundedRectangle(12.0f, 12.0f, static_cast<float>(getWidth()) - 24.0f, static_cast<float>(getHeight()) - 24.0f, 10.0f);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("MOGG DUAL LADDER", 24, 20, 240, 26, juce::Justification::left);

    // Trennlinien: IN | HP-Sektion (3) | SCREAM (1) | LP-Sektion (3) | OUT
    auto inner = getLocalBounds().reduced(24);
    const int colWidth = inner.getWidth() / 9;
    const float topY = static_cast<float>(inner.getY() + 42);
    const float botY = static_cast<float>(inner.getBottom() - 6);

    g.setColour(juce::Colour(0xff353840));
    g.drawLine(static_cast<float>(inner.getX() + colWidth),     topY, static_cast<float>(inner.getX() + colWidth),     botY, 1.0f);
    g.drawLine(static_cast<float>(inner.getX() + colWidth * 4), topY, static_cast<float>(inner.getX() + colWidth * 4), botY, 1.0f);
    g.drawLine(static_cast<float>(inner.getX() + colWidth * 5), topY, static_cast<float>(inner.getX() + colWidth * 5), botY, 1.0f);
    g.drawLine(static_cast<float>(inner.getX() + colWidth * 8), topY, static_cast<float>(inner.getX() + colWidth * 8), botY, 1.0f);
}

void MoggDualLadderAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(24);
    auto topBar = area.removeFromTop(28);

    linkButton.setBounds(topBar.removeFromRight(115));

    const int switchTotalWidth = 216;
    const int btnWidth = switchTotalWidth / 4;
    auto switchBounds = juce::Rectangle<int>(
        (getWidth() - switchTotalWidth) / 2,
        topBar.getY() + 1,
        switchTotalWidth,
        24
    );

    for (size_t i = 0; i < slopeButtons.size(); ++i) {
        slopeButtons[i].setBounds(switchBounds.removeFromLeft(btnWidth));
    }

    area.removeFromTop(12);
    const int colWidth = area.getWidth() / 9;

    auto placeControl = [&](juce::Slider& s, juce::Label& l, juce::ComboBox* modeBox = nullptr) {
        auto col = area.removeFromLeft(colWidth).reduced(4);
        l.setBounds(col.removeFromTop(20));

        auto modeSlot = col.removeFromBottom(26);
        if (modeBox != nullptr) {
            modeBox->setBounds(modeSlot.withSizeKeepingCentre(88, 22));
        }

        s.setBounds(col);
    };

    placeControl(inGainSlider,  inGainLabel);
    placeControl(hpFreqSlider,  hpFreqLabel);
    placeControl(hpResSlider,   hpResLabel);
    placeControl(hpSatSlider,   hpSatLabel, &hpSatModeBox);
    placeControl(screamSlider,  screamLabel);
    placeControl(lpFreqSlider,  lpFreqLabel);
    placeControl(lpResSlider,   lpResLabel);
    placeControl(lpSatSlider,   lpSatLabel, &lpSatModeBox);
    placeControl(outGainSlider, outGainLabel);
}
