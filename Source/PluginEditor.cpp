#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace {
    const juce::Colour kChassisBlack   { 0xff0d0e11 };
    const juce::Colour kPanelDark      { 0xff14161b };
    const juce::Colour kRecessBlack    { 0xff090a0c };
    const juce::Colour kSilverLight    { 0xffe6ebf2 };
    const juce::Colour kSilverMid      { 0xffaeb7c4 };
    const juce::Colour kSilverDark     { 0xff555c68 };
    const juce::Colour kCrispWhite     { 0xfff7f9fc };
    const juce::Colour kNeonTurquoise  { 0xff00f5d4 };
    const juce::Colour kNeonPink       { 0xffff2a85 };
}

MoggLookAndFeel::MoggLookAndFeel() {
    setColour(juce::PopupMenu::backgroundColourId, kPanelDark);
    setColour(juce::PopupMenu::textColourId, kCrispWhite);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kNeonTurquoise.withAlpha(0.28f));
    setColour(juce::PopupMenu::highlightedTextColourId, kCrispWhite);
}

void MoggLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPosProportional, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider& slider) {
    const auto bounds  = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(width), static_cast<float>(height)).reduced(4.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre  = bounds.getCentre();
    const float angle  = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    const auto accent   = slider.findColour(juce::Slider::rotarySliderFillColourId);
    const bool dualNeon = static_cast<bool>(slider.getProperties().getWithDefault("dualNeon", false));

    // 1. Klassische Moog-Skalenstriche (11 Ticks rund um den Regler)
    const float tickOuter = radius - 1.0f;
    const float tickInner = radius - 6.0f;
    for (int i = 0; i <= 10; ++i) {
        const float t = static_cast<float>(i) / 10.0f;
        const float a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
        const bool isMajor = (i == 0 || i == 5 || i == 10);

        const float sinA = std::sin(a);
        const float cosA = -std::cos(a);

        g.setColour(isMajor ? kCrispWhite.withAlpha(0.85f) : kSilverMid.withAlpha(0.45f));
        g.drawLine(centre.x + sinA * (isMajor ? tickInner - 1.5f : tickInner),
                   centre.y + cosA * (isMajor ? tickInner - 1.5f : tickInner),
                   centre.x + sinA * tickOuter,
                   centre.y + cosA * tickOuter,
                   isMajor ? 1.5f : 1.0f);
    }

    // 2. Hintergrund-Bogen & leuchtender Neon-Wertbogen
    const float arcRadius = radius - 9.5f;
    juce::Path bgArc;
    bgArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(kRecessBlack);
    g.strokePath(bgArc, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPosProportional > 0.001f) {
        juce::Path valArc;
        valArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle, true);

        if (dualNeon) {
            juce::ColourGradient grad(kNeonPink,      bounds.getX(), centre.y,
                                      kNeonTurquoise, bounds.getRight(), centre.y, false);
            g.setGradientFill(grad);
        } else {
            g.setColour(accent.withAlpha(0.24f));
            g.strokePath(valArc, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(accent);
        }
        g.strokePath(valArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 3. Äußerer gebürsteter Silber-Sockel (Moog Skirt)
    const float skirtRadius = radius - 14.0f;
    if (skirtRadius <= 6.0f) return;

    g.setColour(juce::Colours::black.withAlpha(0.65f));
    g.fillEllipse(centre.x - skirtRadius + 1.5f, centre.y - skirtRadius + 3.0f, skirtRadius * 2.0f, skirtRadius * 2.0f);

    juce::ColourGradient skirtGrad(kSilverLight, centre.x - skirtRadius, centre.y - skirtRadius,
                                   kSilverDark,  centre.x + skirtRadius, centre.y + skirtRadius, false);
    g.setGradientFill(skirtGrad);
    g.fillEllipse(centre.x - skirtRadius, centre.y - skirtRadius, skirtRadius * 2.0f, skirtRadius * 2.0f);

    // 4. Geriffelter schwarzer Moog-Drehkörper
    const float bodyRadius = skirtRadius * 0.86f;
    constexpr int numFlutes = 18;
    juce::Path flutedBody;
    const int steps = numFlutes * 4;
    for (int i = 0; i <= steps; ++i) {
        const float theta = (static_cast<float>(i) / static_cast<float>(steps)) * juce::MathConstants<float>::twoPi + angle;
        const float fluteWave = std::cos(static_cast<float>(numFlutes) * (theta - angle));
        const float r = bodyRadius - (0.65f * (1.0f - fluteWave));
        const float px = centre.x + std::sin(theta) * r;
        const float py = centre.y - std::cos(theta) * r;
        if (i == 0) flutedBody.startNewSubPath(px, py);
        else        flutedBody.lineTo(px, py);
    }
    flutedBody.closeSubPath();

    juce::ColourGradient bodyGrad(juce::Colour(0xff252830), centre.x - bodyRadius * 0.5f, centre.y - bodyRadius * 0.5f,
                                  juce::Colour(0xff0b0c0e), centre.x + bodyRadius * 0.7f, centre.y + bodyRadius * 0.7f, true);
    g.setGradientFill(bodyGrad);
    g.fillPath(flutedBody);

    g.setColour(kSilverDark.withAlpha(0.6f));
    g.strokePath(flutedBody, juce::PathStrokeType(0.9f));

    // 5. Klassische Moog-Silberkappe in der Mitte
    const float capRadius = bodyRadius * 0.62f;
    juce::ColourGradient capGrad(kCrispWhite, centre.x - capRadius * 0.65f, centre.y - capRadius * 0.65f,
                                 kSilverMid,  centre.x + capRadius * 0.75f, centre.y + capRadius * 0.75f, false);
    g.setGradientFill(capGrad);
    g.fillEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f);

    g.setColour(kSilverDark);
    g.drawEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f, 1.0f);

    // 6. Kontrastreicher Zeigerstrich (Moog Pointer)
    const float sinPtr = std::sin(angle);
    const float cosPtr = -std::cos(angle);
    const auto activePtrColour = dualNeon ? kNeonTurquoise : (accent == kSilverLight ? kChassisBlack : accent);

    g.setColour(kChassisBlack);
    g.drawLine(centre.x + sinPtr * (capRadius * 0.22f),
               centre.y + cosPtr * (capRadius * 0.22f),
               centre.x + sinPtr * (skirtRadius - 0.8f),
               centre.y + cosPtr * (skirtRadius - 0.8f),
               2.6f);

    g.setColour(activePtrColour == kChassisBlack ? kCrispWhite : activePtrColour);
    g.drawLine(centre.x + sinPtr * (capRadius * 0.25f),
               centre.y + cosPtr * (capRadius * 0.25f),
               centre.x + sinPtr * (skirtRadius - 1.2f),
               centre.y + cosPtr * (skirtRadius - 1.2f),
               1.5f);
}

void MoggLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                           const juce::Colour&,
                                           bool shouldDrawButtonAsHighlighted,
                                           bool) {
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const bool isOn = button.getToggleState();

    // Links Neon-Pink (6 dB / 12 dB), rechts Neon-Türkis (24 dB / 48 dB)
    juce::Colour onColour = kNeonPink;
    if (button.getButtonText() == "24 dB" || button.getButtonText() == "48 dB") {
        onColour = kNeonTurquoise;
    }

    if (isOn) {
        g.setColour(onColour.withAlpha(0.22f));
        g.fillRoundedRectangle(bounds.expanded(1.5f), 4.0f);

        juce::ColourGradient btnGrad(onColour.brighter(0.15f), bounds.getX(), bounds.getY(),
                                     onColour.darker(0.15f),   bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(btnGrad);
        g.fillRoundedRectangle(bounds, 3.0f);

        g.setColour(kCrispWhite.withAlpha(0.85f));
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    } else {
        g.setColour(shouldDrawButtonAsHighlighted ? juce::Colour(0xff20242c) : kRecessBlack);
        g.fillRoundedRectangle(bounds, 3.0f);

        g.setColour(kSilverDark.withAlpha(0.75f));
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    }
}

void MoggLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                     bool, bool) {
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.setColour(button.getToggleState() ? kChassisBlack : kSilverLight);
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

void MoggLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawButtonAsHighlighted, bool) {
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const bool isOn = button.getToggleState();

    g.setColour(isOn ? juce::Colour(0xff1b1f27) : (shouldDrawButtonAsHighlighted ? juce::Colour(0xff191c22) : kRecessBlack));
    g.fillRoundedRectangle(bounds, 4.0f);

    if (isOn) {
        juce::ColourGradient borderGrad(kNeonPink,      bounds.getX(), bounds.getY(),
                                        kNeonTurquoise, bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill(borderGrad);
        g.drawRoundedRectangle(bounds, 4.0f, 1.4f);
    } else {
        g.setColour(kSilverDark.withAlpha(0.8f));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }

    // LED-Statuspunkt links im Schalter (passend zur rechten Seite in Neon-Türkis)
    auto ledArea = bounds.removeFromLeft(22.0f).withSizeKeepingCentre(8.0f, 8.0f);
    if (isOn) {
        g.setColour(kNeonTurquoise.withAlpha(0.35f));
        g.fillEllipse(ledArea.expanded(3.0f));
        g.setColour(kNeonTurquoise);
    } else {
        g.setColour(kSilverDark);
    }
    g.fillEllipse(ledArea);

    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.setColour(isOn ? kCrispWhite : kSilverMid);
    g.drawText(button.getButtonText(), bounds.reduced(2.0f, 0.0f), juce::Justification::centredLeft, false);
}

void MoggLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                   int, int, int, int, juce::ComboBox& box) {
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    const auto accent = box.findColour(juce::ComboBox::arrowColourId);

    g.setColour(kRecessBlack);
    g.fillRoundedRectangle(bounds, 3.5f);

    g.setColour(accent.withAlpha(0.75f));
    g.drawRoundedRectangle(bounds, 3.5f, 1.0f);

    // Kleiner Pfeil rechts
    const float arrowX = static_cast<float>(width) - 13.0f;
    const float arrowY = static_cast<float>(height) * 0.5f;
    juce::Path arrow;
    arrow.addTriangle(arrowX - 3.5f, arrowY - 2.0f,
                      arrowX + 3.5f, arrowY - 2.0f,
                      arrowX,        arrowY + 2.5f);
    g.setColour(accent);
    g.fillPath(arrow);
}

// ==============================================================================

MoggDualLadderAudioProcessorEditor::MoggDualLadderAudioProcessorEditor(MoggDualLadderAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&moggLookAndFeel);

    setupRotarySlider(inGainSlider,  inGainLabel,  "IN LEVEL",  kSilverLight);
    setupRotarySlider(hpFreqSlider,  hpFreqLabel,  "HP CUTOFF", kNeonPink);
    setupRotarySlider(hpResSlider,   hpResLabel,   "HP RES",    kNeonPink);
    setupRotarySlider(hpSatSlider,   hpSatLabel,   "HP SAT",    kNeonPink);
    setupRotarySlider(dirtSlider,    dirtLabel,    "DIRT",      kNeonTurquoise, true);
    setupRotarySlider(lpFreqSlider,  lpFreqLabel,  "LP CUTOFF", kNeonTurquoise);
    setupRotarySlider(lpResSlider,   lpResLabel,   "LP RES",    kNeonTurquoise);
    setupRotarySlider(lpSatSlider,   lpSatLabel,   "LP SAT",    kNeonTurquoise);
    setupRotarySlider(outGainSlider, outGainLabel, "OUT LEVEL", kSilverLight);

    setupSatModeBox(hpSatModeBox, kNeonPink);
    setupSatModeBox(lpSatModeBox, kNeonTurquoise);

    addAndMakeVisible(linkButton);

    for (size_t i = 0; i < slopeButtons.size(); ++i) {
        auto& btn = slopeButtons[i];
        btn.setRadioGroupId(1001);
        btn.setClickingTogglesState(true);

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
    dirtAttachment      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "dirt",      dirtSlider);
    lpFreqAttachment    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "lpFreq",    lpFreqSlider);
    lpResAttachment     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "lpRes",     lpResSlider);
    lpSatAttachment     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "lpSat",     lpSatSlider);
    lpSatModeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.apvts, "lpSatMode", lpSatModeBox);
    outGainAttachment   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "outGain",   outGainSlider);
    linkAttachment      = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "link",      linkButton);

    setSize(1060, 322);
}

MoggDualLadderAudioProcessorEditor::~MoggDualLadderAudioProcessorEditor() {
    setLookAndFeel(nullptr);
}

void MoggDualLadderAudioProcessorEditor::setupRotarySlider(juce::Slider& slider, juce::Label& label,
                                                           const juce::String& text, juce::Colour accent,
                                                           bool dualNeon) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 19);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, kRecessBlack);
    slider.setColour(juce::Slider::textBoxOutlineColourId, kSilverDark.withAlpha(0.65f));
    slider.setColour(juce::Slider::textBoxTextColourId, kCrispWhite);
    slider.setColour(juce::Slider::textBoxHighlightColourId, accent.withAlpha(0.35f));
    slider.getProperties().set("dualNeon", dualNeon);
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, kSilverLight);
    addAndMakeVisible(label);
}

void MoggDualLadderAudioProcessorEditor::setupSatModeBox(juce::ComboBox& box, juce::Colour accent) {
    box.addItem("LADDER", 1);
    box.addItem("WARM",   2);
    box.addItem("FOLD",   3);
    box.addItem("GRIT",   4);
    box.setJustificationType(juce::Justification::centred);
    box.setColour(juce::ComboBox::backgroundColourId, kRecessBlack);
    box.setColour(juce::ComboBox::textColourId, kCrispWhite);
    box.setColour(juce::ComboBox::outlineColourId, accent.withAlpha(0.75f));
    box.setColour(juce::ComboBox::arrowColourId, accent);
    addAndMakeVisible(box);
}

void MoggDualLadderAudioProcessorEditor::paint(juce::Graphics& g) {
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());

    // 1. Gebürstete schwarze Moog-Frontplatte
    juce::ColourGradient chassisGrad(juce::Colour(0xff16181e), 0.0f, 0.0f,
                                     kChassisBlack,            0.0f, h, false);
    g.setGradientFill(chassisGrad);
    g.fillAll();

    // Feine horizontale Metall-Bürstung
    g.setColour(kCrispWhite.withAlpha(0.018f));
    for (int y = 2; y < getHeight(); y += 3) {
        g.drawHorizontalLine(y, 22.0f, w - 22.0f);
    }

    // 2. Gebürstete Silber-Seitenteile (Rack-Cheeks) inkl. Moog-Sechskantschrauben
    const float cheekW = 22.0f;
    auto drawSilverCheek = [&](float xPos, bool isLeft) {
        juce::ColourGradient metalGrad(kSilverLight, xPos, 0.0f,
                                       kSilverMid,   xPos + cheekW, 0.0f, false);
        g.setGradientFill(metalGrad);
        g.fillRect(xPos, 0.0f, cheekW, h);

        g.setColour(kCrispWhite.withAlpha(0.7f));
        g.drawVerticalLine(static_cast<int>(isLeft ? xPos + cheekW - 1.0f : xPos), 0.0f, h);
        g.setColour(kSilverDark);
        g.drawVerticalLine(static_cast<int>(isLeft ? xPos : xPos + cheekW - 1.0f), 0.0f, h);

        auto drawScrew = [&](float cy) {
            const float cx = xPos + cheekW * 0.5f;
            g.setColour(kSilverDark);
            g.fillEllipse(cx - 5.0f, cy - 5.0f, 10.0f, 10.0f);
            g.setColour(kSilverLight);
            g.drawEllipse(cx - 5.0f, cy - 5.0f, 10.0f, 10.0f, 1.0f);
            g.setColour(kChassisBlack);
            g.drawLine(cx - 2.6f, cy - 2.6f, cx + 2.6f, cy + 2.6f, 1.3f);
            g.drawLine(cx - 2.6f, cy + 2.6f, cx + 2.6f, cy - 2.6f, 1.3f);
        };
        drawScrew(22.0f);
        drawScrew(h - 22.0f);
    };

    drawSilverCheek(0.0f, true);
    drawSilverCheek(w - cheekW, false);

    // 3. Äußerer weiß/silberner Siebdruck-Rahmen auf der Frontplatte
    auto faceplate = juce::Rectangle<float>(cheekW + 10.0f, 10.0f, w - (cheekW + 10.0f) * 2.0f, h - 20.0f);
    g.setColour(kPanelDark.withAlpha(0.75f));
    g.fillRoundedRectangle(faceplate, 6.0f);
    g.setColour(kSilverMid.withAlpha(0.45f));
    g.drawRoundedRectangle(faceplate, 6.0f, 1.2f);

    // 4. Moog-inspirierte Kopfzeile & Neon-Zierlinie (Links Pink, Rechts Türkis)
    const float headerLeft  = faceplate.getX() + 14.0f;
    const float headerRight = faceplate.getRight() - 14.0f;

    g.setColour(kCrispWhite);
    g.setFont(juce::FontOptions(21.0f, juce::Font::bold));
    g.drawText("MOGG", static_cast<int>(headerLeft), 16, 86, 26, juce::Justification::left);

    g.setColour(kSilverLight);
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText("DUAL LADDER", static_cast<int>(headerLeft + 76.0f), 20, 150, 20, juce::Justification::left);

    const float lineY = 48.0f;
    const float midX  = w * 0.5f;
    g.setColour(kNeonPink);
    g.fillRect(headerLeft, lineY, midX - headerLeft, 1.8f);
    g.setColour(kNeonTurquoise);
    g.fillRect(midX, lineY, headerRight - midX, 1.8f);

    // 5. Klassische weiße Moog-Sektionsrahmen & Titel über den 5 Modulen
    auto deck = faceplate.reduced(10.0f, 0.0f);
    deck.setTop(56.0f);
    deck.setBottom(faceplate.getBottom() - 8.0f);

    const float colW = deck.getWidth() / 9.0f;

    auto drawModuleFrame = [&](int startCol, int numCols, const juce::String& title, juce::Colour titleAccent) {
        auto modRect = juce::Rectangle<float>(deck.getX() + colW * static_cast<float>(startCol) + 3.0f,
                                              deck.getY(),
                                              colW * static_cast<float>(numCols) - 6.0f,
                                              deck.getHeight());

        g.setColour(kChassisBlack.withAlpha(0.55f));
        g.fillRoundedRectangle(modRect, 5.0f);

        g.setColour(kCrispWhite.withAlpha(0.28f));
        g.drawRoundedRectangle(modRect, 5.0f, 1.0f);

        auto banner = modRect.removeFromTop(18.0f);
        g.setColour(titleAccent.withAlpha(0.16f));
        g.fillRoundedRectangle(banner.reduced(1.0f, 1.0f), 4.0f);

        g.setColour(titleAccent);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(title, banner.toNearestInt(), juce::Justification::centred, false);
    };

    drawModuleFrame(0, 1, "INPUT",    kSilverLight);
    drawModuleFrame(1, 3, "HIGHPASS", kNeonPink);
    drawModuleFrame(4, 1, "MOD",      kCrispWhite);
    drawModuleFrame(5, 3, "LOWPASS",  kNeonTurquoise);
    drawModuleFrame(8, 1, "OUTPUT",   kSilverLight);
}

void MoggDualLadderAudioProcessorEditor::resized() {
    const int cheekW = 22;
    auto faceplate = getLocalBounds().reduced(cheekW + 10, 10);

    // Kopfzeile: Slope-Taster mittig, LINK FREQ rechts
    auto topBar = faceplate.reduced(14, 0).removeFromTop(38);
    topBar.removeFromTop(6);

    linkButton.setBounds(topBar.removeFromRight(114).reduced(0, 3));

    const int switchTotalWidth = 236;
    const int btnWidth = switchTotalWidth / 4;
    auto switchBounds = juce::Rectangle<int>(
        (getWidth() - switchTotalWidth) / 2,
        topBar.getY() + 3,
        switchTotalWidth,
        24
    );

    for (size_t i = 0; i < slopeButtons.size(); ++i) {
        slopeButtons[i].setBounds(switchBounds.removeFromLeft(btnWidth).reduced(2, 0));
    }

    // Steuer-Deck unterhalb der Modul-Banner
    auto deck = faceplate.reduced(10, 0);
    deck.setTop(78);
    deck.setBottom(faceplate.getBottom() - 12);

    const int colWidth = deck.getWidth() / 9;

    auto placeControl = [&](juce::Slider& s, juce::Label& l, juce::ComboBox* modeBox = nullptr) {
        auto col = deck.removeFromLeft(colWidth).reduced(5, 0);
        l.setBounds(col.removeFromTop(18));

        auto modeSlot = col.removeFromBottom(25);
        if (modeBox != nullptr) {
            modeBox->setBounds(modeSlot.withSizeKeepingCentre(88, 21));
        }

        s.setBounds(col);
    };

    placeControl(inGainSlider,  inGainLabel);
    placeControl(hpFreqSlider,  hpFreqLabel);
    placeControl(hpResSlider,   hpResLabel);
    placeControl(hpSatSlider,   hpSatLabel, &hpSatModeBox);
    placeControl(dirtSlider,    dirtLabel);
    placeControl(lpFreqSlider,  lpFreqLabel);
    placeControl(lpResSlider,   lpResLabel);
    placeControl(lpSatSlider,   lpSatLabel, &lpSatModeBox);
    placeControl(outGainSlider, outGainLabel);
}
