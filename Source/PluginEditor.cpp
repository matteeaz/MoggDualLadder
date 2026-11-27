#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace {
    // Knob-Stile passend zur Eurorack-Hardware-Vorlage:
    // 0 = Schwarzer geriffelter Metallregler mit Neon-Kantenring
    // 1 = Großer zentraler Hero-Regler mit Pink/Türkis-Verlauf & Skalenkranz
    // 2 = Gefrosteter transparenter Polycarbonat-Regler mit Neon-Kern
    constexpr int kKnobBlackMetal = 0;
    constexpr int kKnobHeroDual   = 1;
    constexpr int kKnobFrosted    = 2;

    const juce::Colour kRailBlack      { 0xff101216 };
    const juce::Colour kOledDark       { 0xff0d0f14 };
    const juce::Colour kEtchedInk      { 0xff161920 };
    const juce::Colour kSilverBright   { 0xffe4e8ef };
    const juce::Colour kSilverBase     { 0xffc6cdd8 };
    const juce::Colour kSilverShadow   { 0xff8d96a5 };
    const juce::Colour kCrispWhite     { 0xfff8faff };
    const juce::Colour kNeonPink       { 0xffff1e8a };
    const juce::Colour kNeonTurquoise  { 0xff00f5d4 };

    void drawNeonTrace(juce::Graphics& g, const juce::Path& path,
                       juce::Colour startCol, juce::Colour endCol,
                       float x1, float x2, float strokeW = 1.45f) {
        if (startCol == endCol) {
            g.setColour(startCol.withAlpha(0.24f));
            g.strokePath(path, juce::PathStrokeType(strokeW + 3.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            g.setColour(startCol.withAlpha(0.92f));
            g.strokePath(path, juce::PathStrokeType(strokeW, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        } else {
            juce::ColourGradient glowGrad(startCol.withAlpha(0.26f), x1, 0.0f, endCol.withAlpha(0.26f), x2, 0.0f, false);
            g.setGradientFill(glowGrad);
            g.strokePath(path, juce::PathStrokeType(strokeW + 3.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));

            juce::ColourGradient lineGrad(startCol.withAlpha(0.95f), x1, 0.0f, endCol.withAlpha(0.95f), x2, 0.0f, false);
            g.setGradientFill(lineGrad);
            g.strokePath(path, juce::PathStrokeType(strokeW, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        }
    }

    void drawEurorackJack(juce::Graphics& g, float cx, float cy, juce::Colour neonCol) {
        // Äußerer Neon-Leuchtring auf der Silberplatte
        g.setColour(neonCol.withAlpha(0.28f));
        g.fillEllipse(cx - 12.5f, cy - 12.5f, 25.0f, 25.0f);
        g.setColour(neonCol.withAlpha(0.95f));
        g.drawEllipse(cx - 9.8f, cy - 9.8f, 19.6f, 19.6f, 1.5f);

        // Silberne Sechskantmutter
        juce::Path hexNut;
        for (int i = 0; i < 6; ++i) {
            const float a = static_cast<float>(i) * juce::MathConstants<float>::pi / 3.0f;
            const float px = cx + std::cos(a) * 8.2f;
            const float py = cy + std::sin(a) * 8.2f;
            if (i == 0) hexNut.startNewSubPath(px, py);
            else        hexNut.lineTo(px, py);
        }
        hexNut.closeSubPath();

        juce::ColourGradient nutGrad(kSilverBright, cx - 7.0f, cy - 7.0f,
                                     kSilverShadow, cx + 7.0f, cy + 7.0f, false);
        g.setGradientFill(nutGrad);
        g.fillPath(hexNut);
        g.setColour(kEtchedInk.withAlpha(0.65f));
        g.strokePath(hexNut, juce::PathStrokeType(0.8f));

        // Chrom-Kragen & schwarze 3,5-mm-Klinkenöffnung
        g.setColour(kSilverBright);
        g.fillEllipse(cx - 5.4f, cy - 5.4f, 10.8f, 10.8f);
        g.setColour(kRailBlack);
        g.fillEllipse(cx - 3.6f, cy - 3.6f, 7.2f, 7.2f);
    }

    void drawHexScrew(juce::Graphics& g, float cx, float cy) {
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillEllipse(cx - 6.0f, cy - 5.0f, 12.5f, 12.5f);

        juce::ColourGradient screwGrad(juce::Colour(0xff2c313b), cx - 5.0f, cy - 5.0f,
                                       kRailBlack,               cx + 5.0f, cy + 5.0f, false);
        g.setGradientFill(screwGrad);
        g.fillEllipse(cx - 5.8f, cy - 5.8f, 11.6f, 11.6f);

        g.setColour(kSilverShadow.withAlpha(0.65f));
        g.drawEllipse(cx - 5.8f, cy - 5.8f, 11.6f, 11.6f, 0.9f);

        // Innensechskant
        juce::Path hex;
        for (int i = 0; i < 6; ++i) {
            const float a = static_cast<float>(i) * juce::MathConstants<float>::pi / 3.0f;
            const float px = cx + std::cos(a) * 2.8f;
            const float py = cy + std::sin(a) * 2.8f;
            if (i == 0) hex.startNewSubPath(px, py);
            else        hex.lineTo(px, py);
        }
        hex.closeSubPath();
        g.setColour(juce::Colour(0xff07080a));
        g.fillPath(hex);
    }
}

MoggLookAndFeel::MoggLookAndFeel() {
    setColour(juce::PopupMenu::backgroundColourId, kOledDark);
    setColour(juce::PopupMenu::textColourId, kCrispWhite);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kNeonTurquoise.withAlpha(0.30f));
    setColour(juce::PopupMenu::highlightedTextColourId, kCrispWhite);
}

void MoggLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPosProportional, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider& slider) {
    const auto bounds  = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(width), static_cast<float>(height)).reduced(3.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre  = bounds.getCentre();
    const float angle  = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    const auto accent    = slider.findColour(juce::Slider::rotarySliderFillColourId);
    const int knobStyle  = static_cast<int>(slider.getProperties().getWithDefault("knobStyle", kKnobBlackMetal));
    const bool isHero    = (knobStyle == kKnobHeroDual);
    const bool isFrosted = (knobStyle == kKnobFrosted);

    // 1. Feiner Skalenkranz für den zentralen Hero-Regler (wie im Foto)
    if (isHero) {
        const float tickOuter = radius - 1.0f;
        const float tickInner = radius - 5.5f;
        constexpr int numTicks = 44;
        for (int i = 0; i <= numTicks; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(numTicks);
            const float a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
            const auto tickCol = kNeonPink.interpolatedWith(kNeonTurquoise, t);
            g.setColour(tickCol.withAlpha(t <= sliderPosProportional + 0.01f ? 0.90f : 0.35f));
            g.drawLine(centre.x + std::sin(a) * tickInner,
                       centre.y - std::cos(a) * tickInner,
                       centre.x + std::sin(a) * tickOuter,
                       centre.y - std::cos(a) * tickOuter,
                       1.1f);
        }
    }

    // 2. Leuchtender Neon-Bogen auf der Silberplatte um den Reglerfuß
    const float arcRadius = radius - (isHero ? 8.5f : 4.5f);
    juce::Path bgArc;
    bgArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(kEtchedInk.withAlpha(0.28f));
    g.strokePath(bgArc, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPosProportional > 0.001f) {
        juce::Path valArc;
        valArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle, true);

        if (isHero) {
            juce::ColourGradient glowGrad(kNeonPink.withAlpha(0.32f), bounds.getX(), centre.y,
                                          kNeonTurquoise.withAlpha(0.32f), bounds.getRight(), centre.y, false);
            g.setGradientFill(glowGrad);
            g.strokePath(valArc, juce::PathStrokeType(7.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::ColourGradient arcGrad(kNeonPink, bounds.getX(), centre.y,
                                         kNeonTurquoise, bounds.getRight(), centre.y, false);
            g.setGradientFill(arcGrad);
            g.strokePath(valArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        } else {
            g.setColour(accent.withAlpha(0.28f));
            g.strokePath(valArc, juce::PathStrokeType(6.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(accent);
            g.strokePath(valArc, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // 3. Reglerkörper & weicher Schlagschatten nach unten rechts
    const float bodyRadius = radius - (isHero ? 13.5f : 8.5f);
    if (bodyRadius <= 6.0f) return;

    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillEllipse(centre.x - bodyRadius + 2.5f, centre.y - bodyRadius + 4.0f, bodyRadius * 2.0f, bodyRadius * 2.0f);

    // Geriffelter Außenumfang
    constexpr int numFlutes = 20;
    juce::Path flutedBody;
    const int steps = numFlutes * 4;
    for (int i = 0; i <= steps; ++i) {
        const float theta = (static_cast<float>(i) / static_cast<float>(steps)) * juce::MathConstants<float>::twoPi + angle;
        const float fluteWave = std::cos(static_cast<float>(numFlutes) * (theta - angle));
        const float r = bodyRadius - (0.75f * (1.0f - fluteWave));
        const float px = centre.x + std::sin(theta) * r;
        const float py = centre.y - std::cos(theta) * r;
        if (i == 0) flutedBody.startNewSubPath(px, py);
        else        flutedBody.lineTo(px, py);
    }
    flutedBody.closeSubPath();

    const float capRadius = bodyRadius * 0.76f;

    if (isFrosted) {
        // Gefrosteter transparenter Polycarbonat-Regler (wie unten rechts im Bild)
        g.setColour(accent.withAlpha(0.25f));
        g.fillEllipse(centre.x - bodyRadius - 1.5f, centre.y - bodyRadius - 1.5f, (bodyRadius + 1.5f) * 2.0f, (bodyRadius + 1.5f) * 2.0f);

        juce::ColourGradient frostGrad(juce::Colour(0xdde6ecf5), centre.x - bodyRadius * 0.6f, centre.y - bodyRadius * 0.6f,
                                       juce::Colour(0xbb8e99aa), centre.x + bodyRadius * 0.8f, centre.y + bodyRadius * 0.8f, false);
        g.setGradientFill(frostGrad);
        g.fillPath(flutedBody);

        g.setColour(kCrispWhite.withAlpha(0.65f));
        g.strokePath(flutedBody, juce::PathStrokeType(0.9f));

        // Innerer Neon-Kern schimmert durch die Kappe
        juce::ColourGradient coreGlow(accent.withAlpha(0.45f), centre.x, centre.y,
                                      juce::Colour(0xcc9ba6b8), centre.x + capRadius, centre.y + capRadius, true);
        g.setGradientFill(coreGlow);
        g.fillEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f);

        g.setColour(accent.withAlpha(0.9f));
        g.drawEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f, 1.4f);
    } else {
        // Schwarzer eloxierter Metallregler
        juce::ColourGradient bodyGrad(juce::Colour(0xff2a2e38), centre.x - bodyRadius * 0.6f, centre.y - bodyRadius * 0.6f,
                                      juce::Colour(0xff0a0b0e), centre.x + bodyRadius * 0.7f, centre.y + bodyRadius * 0.7f, true);
        g.setGradientFill(bodyGrad);
        g.fillPath(flutedBody);

        // Dezenter Neon-Schimmer an den Riffel-Flanken
        if (isHero) {
            juce::ColourGradient flankGrad(kNeonPink.withAlpha(0.35f), centre.x - bodyRadius, centre.y,
                                           kNeonTurquoise.withAlpha(0.35f), centre.x + bodyRadius, centre.y, false);
            g.setGradientFill(flankGrad);
        } else {
            g.setColour(accent.withAlpha(0.28f));
        }
        g.strokePath(flutedBody, juce::PathStrokeType(1.0f));

        // Dunkel eloxierte Metallkappe mit konischem Lichtreflex (Bowtie-Sheen)
        juce::ColourGradient capBase(juce::Colour(0xff232730), centre.x - capRadius, centre.y - capRadius,
                                     juce::Colour(0xff101217), centre.x + capRadius, centre.y + capRadius, false);
        g.setGradientFill(capBase);
        g.fillEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f);

        {
            juce::Graphics::ScopedSaveState state(g);
            juce::Path capClip;
            capClip.addEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f);
            g.reduceClipRegion(capClip);

            // Zwei gegenüberliegende Lichtkeile für den gedrehten Metall-Effekt
            juce::Path sheen1, sheen2;
            sheen1.startNewSubPath(centre.x, centre.y);
            sheen1.lineTo(centre.x - capRadius * 1.1f, centre.y - capRadius * 0.45f);
            sheen1.lineTo(centre.x - capRadius * 0.45f, centre.y - capRadius * 1.1f);
            sheen1.closeSubPath();

            sheen2.startNewSubPath(centre.x, centre.y);
            sheen2.lineTo(centre.x + capRadius * 1.1f, centre.y + capRadius * 0.45f);
            sheen2.lineTo(centre.x + capRadius * 0.45f, centre.y + capRadius * 1.1f);
            sheen2.closeSubPath();

            g.setColour(kCrispWhite.withAlpha(0.14f));
            g.fillPath(sheen1);
            g.fillPath(sheen2);
        }

        // Leuchtender Neon-Kantenring auf der Oberseite der Kappe (Pink/Türkis-Verlauf beim Hero-Regler)
        if (isHero) {
            juce::ColourGradient rimGlow(kNeonPink.withAlpha(0.35f), centre.x - capRadius, centre.y,
                                         kNeonTurquoise.withAlpha(0.35f), centre.x + capRadius, centre.y, false);
            g.setGradientFill(rimGlow);
            g.drawEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f, 3.6f);

            juce::ColourGradient rimGrad(kNeonPink, centre.x - capRadius, centre.y,
                                         kNeonTurquoise, centre.x + capRadius, centre.y, false);
            g.setGradientFill(rimGrad);
            g.drawEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f, 1.8f);
        } else {
            g.setColour(accent.withAlpha(0.30f));
            g.drawEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f, 3.2f);
            g.setColour(accent);
            g.drawEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f, 1.5f);
        }
    }

    // 4. Weißer Zeigerstrich auf der Kappe
    const float sinPtr = std::sin(angle);
    const float cosPtr = -std::cos(angle);
    g.setColour(kCrispWhite);
    g.drawLine(centre.x + sinPtr * (capRadius * 0.25f),
               centre.y + cosPtr * (capRadius * 0.25f),
               centre.x + sinPtr * (capRadius * 0.96f),
               centre.y + cosPtr * (capRadius * 0.96f),
               isHero ? 2.0f : 1.6f);
}

void MoggLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                           const juce::Colour&,
                                           bool shouldDrawButtonAsHighlighted,
                                           bool) {
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const bool isOn = button.getToggleState();

    juce::Colour onColour = kNeonPink;
    if (button.getButtonText() == "24 dB" || button.getButtonText() == "48 dB") {
        onColour = kNeonTurquoise;
    }

    if (isOn) {
        g.setColour(onColour.withAlpha(0.28f));
        g.fillRoundedRectangle(bounds.expanded(1.5f), 4.0f);

        juce::ColourGradient btnGrad(onColour.brighter(0.12f), bounds.getX(), bounds.getY(),
                                     onColour.darker(0.12f),   bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(btnGrad);
        g.fillRoundedRectangle(bounds, 3.5f);

        g.setColour(kCrispWhite.withAlpha(0.9f));
        g.drawRoundedRectangle(bounds, 3.5f, 1.0f);
    } else {
        g.setColour(shouldDrawButtonAsHighlighted ? juce::Colour(0xff232730) : kOledDark);
        g.fillRoundedRectangle(bounds, 3.5f);

        g.setColour(kEtchedInk.withAlpha(0.85f));
        g.drawRoundedRectangle(bounds, 3.5f, 1.1f);
    }
}

void MoggLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                     bool, bool) {
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.setColour(button.getToggleState() ? kRailBlack : kSilverBright);
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

void MoggLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawButtonAsHighlighted, bool) {
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const bool isOn = button.getToggleState();

    g.setColour(isOn ? juce::Colour(0xff161920) : (shouldDrawButtonAsHighlighted ? juce::Colour(0xff1d2129) : kOledDark));
    g.fillRoundedRectangle(bounds, 4.0f);

    if (isOn) {
        juce::ColourGradient borderGrad(kNeonPink, bounds.getX(), bounds.getY(),
                                        kNeonTurquoise, bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill(borderGrad);
        g.drawRoundedRectangle(bounds, 4.0f, 1.5f);
    } else {
        g.setColour(kEtchedInk.withAlpha(0.85f));
        g.drawRoundedRectangle(bounds, 4.0f, 1.1f);
    }

    auto ledArea = bounds.removeFromLeft(22.0f).withSizeKeepingCentre(8.0f, 8.0f);
    if (isOn) {
        g.setColour(kNeonTurquoise.withAlpha(0.38f));
        g.fillEllipse(ledArea.expanded(3.0f));
        g.setColour(kNeonTurquoise);
    } else {
        g.setColour(kSilverShadow);
    }
    g.fillEllipse(ledArea);

    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.setColour(isOn ? kCrispWhite : kSilverBright);
    g.drawText(button.getButtonText(), bounds.reduced(2.0f, 0.0f), juce::Justification::centredLeft, false);
}

void MoggLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                   int, int, int, int, juce::ComboBox& box) {
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    const auto accent = box.findColour(juce::ComboBox::arrowColourId);

    g.setColour(kOledDark);
    g.fillRoundedRectangle(bounds, 3.5f);

    g.setColour(accent.withAlpha(0.85f));
    g.drawRoundedRectangle(bounds, 3.5f, 1.1f);

    const float arrowX = static_cast<float>(width) - 12.0f;
    const float arrowY = static_cast<float>(height) * 0.5f;
    juce::Path arrow;
    arrow.addTriangle(arrowX - 3.2f, arrowY - 1.8f,
                      arrowX + 3.2f, arrowY - 1.8f,
                      arrowX,        arrowY + 2.4f);
    g.setColour(accent);
    g.fillPath(arrow);
}

// ==============================================================================

MoggDualLadderAudioProcessorEditor::MoggDualLadderAudioProcessorEditor(MoggDualLadderAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&moggLookAndFeel);

    setupRotarySlider(inGainSlider,  inGainLabel,  "IN LEVEL",  kNeonPink,      kKnobBlackMetal);
    setupRotarySlider(hpFreqSlider,  hpFreqLabel,  "HP CUTOFF", kNeonPink,      kKnobBlackMetal);
    setupRotarySlider(hpResSlider,   hpResLabel,   "HP RES",    kNeonPink,      kKnobBlackMetal);
    setupRotarySlider(hpSatSlider,   hpSatLabel,   "HP SAT",    kNeonPink,      kKnobFrosted);
    setupRotarySlider(dirtSlider,    dirtLabel,    "DIRT",      kNeonTurquoise, kKnobHeroDual);
    setupRotarySlider(lpFreqSlider,  lpFreqLabel,  "LP CUTOFF", kNeonTurquoise, kKnobBlackMetal);
    setupRotarySlider(lpResSlider,   lpResLabel,   "LP RES",    kNeonTurquoise, kKnobBlackMetal);
    setupRotarySlider(lpSatSlider,   lpSatLabel,   "LP SAT",    kNeonTurquoise, kKnobFrosted);
    setupRotarySlider(outGainSlider, outGainLabel, "OUT LEVEL", kNeonTurquoise, kKnobBlackMetal);

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

    setSize(1080, 354);
}

MoggDualLadderAudioProcessorEditor::~MoggDualLadderAudioProcessorEditor() {
    setLookAndFeel(nullptr);
}

void MoggDualLadderAudioProcessorEditor::setupRotarySlider(juce::Slider& slider, juce::Label& label,
                                                           const juce::String& text, juce::Colour accent,
                                                           int knobStyle) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 18);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, kOledDark);
    slider.setColour(juce::Slider::textBoxOutlineColourId, accent.withAlpha(0.65f));
    slider.setColour(juce::Slider::textBoxTextColourId, kCrispWhite);
    slider.setColour(juce::Slider::textBoxHighlightColourId, accent.withAlpha(0.35f));
    slider.getProperties().set("knobStyle", knobStyle);
    addAndMakeVisible(slider);

    // Dunkel geätzte Frontplatten-Beschriftung auf gebürstetem Silber (wie im Foto)
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, kEtchedInk);
    addAndMakeVisible(label);
}

void MoggDualLadderAudioProcessorEditor::setupSatModeBox(juce::ComboBox& box, juce::Colour accent) {
    box.addItem("LADDER", 1);
    box.addItem("WARM",   2);
    box.addItem("FOLD",   3);
    box.addItem("GRIT",   4);
    box.setJustificationType(juce::Justification::centred);
    box.setColour(juce::ComboBox::backgroundColourId, kOledDark);
    box.setColour(juce::ComboBox::textColourId, kCrispWhite);
    box.setColour(juce::ComboBox::outlineColourId, accent.withAlpha(0.8f));
    box.setColour(juce::ComboBox::arrowColourId, accent);
    addAndMakeVisible(box);
}

void MoggDualLadderAudioProcessorEditor::rebuildBrushedMetalTexture() {
    const int w = getWidth();
    const int h = getHeight();
    if (w <= 0 || h <= 0) return;

    brushedAluminumCache = juce::Image(juce::Image::ARGB, w, h, true);
    juce::Graphics g(brushedAluminumCache);

    // 1. Metallischer Grundverlauf (helles gebürstetes Silber mit diagonalem Lichtschimmer)
    juce::ColourGradient baseMetal(kSilverBase,   0.0f, 0.0f,
                                   kSilverShadow, static_cast<float>(w), static_cast<float>(h), false);
    baseMetal.addColour(0.35, kSilverBright);
    baseMetal.addColour(0.68, kSilverBase);
    g.setGradientFill(baseMetal);
    g.fillAll();

    // 2. Feine horizontale Aluminium-Bürstung
    juce::Random rng(424242);
    for (int y = 0; y < h; ++y) {
        const float streakAlpha = rng.nextFloat() * 0.045f;
        g.setColour((rng.nextBool() ? kCrispWhite : kEtchedInk).withAlpha(streakAlpha));
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(w));
    }

    // 3. Subtile Vignette zu den Plattenrändern hin
    juce::ColourGradient edgeVignette(juce::Colours::transparentBlack, static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.5f,
                                      kEtchedInk.withAlpha(0.22f), 0.0f, 0.0f, true);
    g.setGradientFill(edgeVignette);
    g.fillAll();
}

void MoggDualLadderAudioProcessorEditor::paint(juce::Graphics& g) {
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const float railW = 24.0f;

    // 1. Gebürstete Silber-Frontplatte aus Cache zeichnen
    if (brushedAluminumCache.isValid()) {
        g.drawImageAt(brushedAluminumCache, 0, 0);
    }

    // 2. Matte schwarze Eurorack-Seitenschienen links & rechts
    auto drawSideRail = [&](float xPos, bool isLeft) {
        juce::ColourGradient railGrad(juce::Colour(0xff1c2028), xPos, 0.0f,
                                      kRailBlack,               xPos + railW, 0.0f, false);
        g.setGradientFill(railGrad);
        g.fillRect(xPos, 0.0f, railW, h);

        // Metallische Fase zur Silberplatte hin
        g.setColour(kCrispWhite.withAlpha(0.55f));
        g.drawVerticalLine(static_cast<int>(isLeft ? xPos + railW : xPos - 1.0f), 0.0f, h);
        g.setColour(juce::Colours::black.withAlpha(0.75f));
        g.drawVerticalLine(static_cast<int>(isLeft ? xPos + railW - 1.0f : xPos), 0.0f, h);
    };
    drawSideRail(0.0f, true);
    drawSideRail(w - railW, false);

    // 3. Äußere Silber-Fase & 4 dunkle Eurorack-Sechskantschrauben in den Ecken
    auto plate = juce::Rectangle<float>(railW + 2.0f, 2.0f, w - (railW + 2.0f) * 2.0f, h - 4.0f);
    g.setColour(kCrispWhite.withAlpha(0.55f));
    g.drawRoundedRectangle(plate, 3.0f, 1.0f);

    drawHexScrew(g, plate.getX() + 14.0f,     16.0f);
    drawHexScrew(g, plate.getRight() - 14.0f, 16.0f);
    drawHexScrew(g, plate.getX() + 14.0f,     h - 16.0f);
    drawHexScrew(g, plate.getRight() - 14.0f, h - 16.0f);

    // 4. Geätzte 45°-Leiterbahnen (Links Neon-Pink, Mitte Pink->Türkis, Rechts Neon-Türkis)
    const float leftEdge  = plate.getX() + 6.0f;
    const float rightEdge = plate.getRight() - 6.0f;
    const float midX      = w * 0.5f;

    // Linke Leiterbahnen (Neon-Pink)
    juce::Path leftTraces;
    leftTraces.startNewSubPath(leftEdge, 58.0f);
    leftTraces.lineTo(leftEdge + 115.0f, 58.0f);
    leftTraces.lineTo(leftEdge + 145.0f, 88.0f);
    leftTraces.lineTo(leftEdge + 265.0f, 88.0f);

    leftTraces.startNewSubPath(leftEdge, 72.0f);
    leftTraces.lineTo(leftEdge + 95.0f, 72.0f);
    leftTraces.lineTo(leftEdge + 125.0f, 102.0f);
    leftTraces.lineTo(leftEdge + 240.0f, 102.0f);

    leftTraces.startNewSubPath(leftEdge, 148.0f);
    leftTraces.lineTo(leftEdge + 45.0f, 148.0f);
    leftTraces.lineTo(leftEdge + 72.0f, 175.0f);
    leftTraces.lineTo(leftEdge + 72.0f, 290.0f);
    leftTraces.lineTo(leftEdge + 98.0f, 316.0f);

    leftTraces.startNewSubPath(leftEdge + 185.0f, 286.0f);
    leftTraces.lineTo(leftEdge + 215.0f, 316.0f);
    leftTraces.lineTo(midX - 55.0f, 316.0f);

    drawNeonTrace(g, leftTraces, kNeonPink, kNeonPink, leftEdge, midX);

    // Rechte Leiterbahnen (Neon-Türkis)
    juce::Path rightTraces;
    rightTraces.startNewSubPath(rightEdge, 58.0f);
    rightTraces.lineTo(rightEdge - 115.0f, 58.0f);
    rightTraces.lineTo(rightEdge - 145.0f, 88.0f);
    rightTraces.lineTo(rightEdge - 265.0f, 88.0f);

    rightTraces.startNewSubPath(rightEdge, 72.0f);
    rightTraces.lineTo(rightEdge - 95.0f, 72.0f);
    rightTraces.lineTo(rightEdge - 125.0f, 102.0f);
    rightTraces.lineTo(rightEdge - 240.0f, 102.0f);

    rightTraces.startNewSubPath(rightEdge, 148.0f);
    rightTraces.lineTo(rightEdge - 45.0f, 148.0f);
    rightTraces.lineTo(rightEdge - 72.0f, 175.0f);
    rightTraces.lineTo(rightEdge - 72.0f, 290.0f);
    rightTraces.lineTo(rightEdge - 98.0f, 316.0f);

    rightTraces.startNewSubPath(rightEdge - 185.0f, 286.0f);
    rightTraces.lineTo(rightEdge - 215.0f, 316.0f);
    rightTraces.lineTo(midX + 55.0f, 316.0f);

    drawNeonTrace(g, rightTraces, kNeonTurquoise, kNeonTurquoise, midX, rightEdge);

    // Zentrale Verbindungs-Leiterbahn mit Pink -> Türkis Verlauf
    juce::Path centerTraces;
    centerTraces.startNewSubPath(midX - 185.0f, 118.0f);
    centerTraces.lineTo(midX - 85.0f, 118.0f);
    centerTraces.lineTo(midX - 55.0f, 92.0f);
    centerTraces.lineTo(midX + 55.0f, 92.0f);
    centerTraces.lineTo(midX + 85.0f, 118.0f);
    centerTraces.lineTo(midX + 185.0f, 118.0f);

    centerTraces.startNewSubPath(midX - 55.0f, 316.0f);
    centerTraces.lineTo(midX + 55.0f, 316.0f);

    drawNeonTrace(g, centerTraces, kNeonPink, kNeonTurquoise, midX - 185.0f, midX + 185.0f, 1.6f);

    // 5. Geätztes "MOGG DUAL LADDER" Logo oben links (wie in der Eurorack-Vorlage)
    const int logoX = static_cast<int>(plate.getX() + 34.0f);
    g.setColour(kNeonPink.withAlpha(0.28f));
    g.setFont(juce::FontOptions(23.0f, juce::Font::bold));
    g.drawText("MOGG", logoX + 1, 11, 100, 24, juce::Justification::left);

    g.setColour(kEtchedInk);
    g.drawText("MOGG", logoX, 10, 100, 24, juce::Justification::left);

    g.setColour(kEtchedInk.withAlpha(0.85f));
    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    g.drawText("DUAL LADDER", logoX + 2, 32, 130, 14, juce::Justification::left);

    // 6. Vertikale 12-Segment Neon-LED-Anzeige rechts (Pink unten -> Türkis oben, wie im Foto)
    const float ledX = plate.getRight() - 22.0f;
    const float ledTop = 102.0f;
    constexpr int numLeds = 12;
    for (int i = 0; i < numLeds; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(numLeds - 1);
        const auto ledCol = kNeonTurquoise.interpolatedWith(kNeonPink, t);
        const float ly = ledTop + static_cast<float>(i) * 14.0f;
        auto seg = juce::Rectangle<float>(ledX, ly, 8.5f, 10.0f);

        g.setColour(ledCol.withAlpha(0.26f));
        g.fillRoundedRectangle(seg.expanded(1.5f), 2.0f);

        g.setColour(ledCol.withAlpha(0.85f));
        g.fillRoundedRectangle(seg, 1.5f);

        g.setColour(kEtchedInk.withAlpha(0.55f));
        g.drawRoundedRectangle(seg, 1.5f, 0.8f);
    }

    // 7. Dekorative 3,5-mm-Eurorack-Patchbuchsen entlang der unteren Kante
    const float jackY = h - 22.0f;
    const std::array<float, 10> jackOffsets {
        -420.0f, -330.0f, -240.0f, -150.0f, -60.0f,
          60.0f,  150.0f,  240.0f,  330.0f, 420.0f
    };
    for (size_t i = 0; i < jackOffsets.size(); ++i) {
        const auto col = (i < 5) ? kNeonPink : kNeonTurquoise;
        drawEurorackJack(g, midX + jackOffsets[i], jackY, col);
    }
}

void MoggDualLadderAudioProcessorEditor::resized() {
    rebuildBrushedMetalTexture();

    const int railW = 24;
    auto plate = getLocalBounds().reduced(railW + 2, 2);

    // Kopfzeile: Slope-Taster mittig, LINK FREQ rechts vor der Eckschraube
    auto topBar = plate.reduced(34, 0).removeFromTop(42);
    topBar.removeFromTop(8);

    linkButton.setBounds(topBar.removeFromRight(112).reduced(0, 4));

    const int switchTotalWidth = 244;
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

    // Steuer-Deck oberhalb der unteren Eurorack-Patchbuchsen
    auto deck = plate.reduced(28, 0);
    deck.setTop(74);
    deck.setBottom(getHeight() - 48);

    const int colWidth = deck.getWidth() / 9;

    auto placeControl = [&](juce::Slider& s, juce::Label& l,
                            juce::ComboBox* modeBox = nullptr,
                            bool isCenterHero = false) {
        auto col = deck.removeFromLeft(colWidth).reduced(isCenterHero ? 0 : 4, 0);
        l.setBounds(col.removeFromTop(17));

        auto modeSlot = col.removeFromBottom(24);
        if (modeBox != nullptr) {
            modeBox->setBounds(modeSlot.withSizeKeepingCentre(84, 20));
        }

        if (isCenterHero) {
            // Zentraler DIRT-Regler etwas größer wie der Hauptregler im Foto
            s.setBounds(col.expanded(8, 6));
        } else {
            s.setBounds(col);
        }
    };

    placeControl(inGainSlider,  inGainLabel);
    placeControl(hpFreqSlider,  hpFreqLabel);
    placeControl(hpResSlider,   hpResLabel);
    placeControl(hpSatSlider,   hpSatLabel, &hpSatModeBox);
    placeControl(dirtSlider,    dirtLabel, nullptr, true);
    placeControl(lpFreqSlider,  lpFreqLabel);
    placeControl(lpResSlider,   lpResLabel);
    placeControl(lpSatSlider,   lpSatLabel, &lpSatModeBox);
    placeControl(outGainSlider, outGainLabel);
}
