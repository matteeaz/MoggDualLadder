#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace {
    // Regler-Hierarchie:
    // 0 = Standard Moog-Poti (IN/OUT LEVEL, RES, SAT AMT)
    // 1 = Hero Cutoff-Regler (riesig mit leuchtendem Radial-Skalenkranz)
    // 2 = Zentraler DIRT-Regler (mittelgroß mit Pink->Türkis-Verlauf)
    constexpr int kRoleStandard = 0;
    constexpr int kRoleHero     = 1;
    constexpr int kRoleCenter   = 2;

    const juce::Colour kChassisBlack     { 0xff0d0e12 };
    const juce::Colour kModuleDark       { 0xff111318 };
    const juce::Colour kRecessBlack      { 0xff08090c };
    const juce::Colour kSilverLight      { 0xffe4e9f0 };
    const juce::Colour kSilverMid        { 0xffa7b0bd };
    const juce::Colour kMediumGray       { 0xff7e8694 };
    const juce::Colour kSilverDark       { 0xff4e5561 };
    const juce::Colour kCrispWhite       { 0xfff7f9fc };
    const juce::Colour kPureWhite        { 0xffffffff };
    const juce::Colour kMeterGrayLow     { 0xff5a6270 };

    // Vintage-Pastell-Farbtöne
    const juce::Colour kPastelPink       { 0xfff28ab2 };
    const juce::Colour kPastelTurquoise  { 0xff7ad7c8 };

    float linearToMeterNorm(float peak) noexcept {
        if (peak <= 1.0e-5f) return 0.0f;
        const float db = juce::Decibels::gainToDecibels(peak, -48.0f);
        // -48 dBFS -> 0.0 (Grau), 0 dBFS -> 1.0 (Ganz Weiß)
        return juce::jlimit(0.0f, 1.0f, (db + 48.0f) / 48.0f);
    }

    void drawStereoLedMeter(juce::Graphics& g, juce::Rectangle<int> area, float normL, float normR) {
        auto box = area.toFloat();
        g.setColour(kRecessBlack);
        g.fillRoundedRectangle(box, 4.0f);
        g.setColour(kMediumGray.withAlpha(0.75f));
        g.drawRoundedRectangle(box, 4.0f, 1.0f);

        auto inner = box.reduced(5.0f, 5.0f);

        // Kopfzeile: "dBFS"
        auto unitRow = inner.removeFromTop(12.0f);
        g.setColour(kSilverMid);
        g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        g.drawText("dBFS", unitRow.toNearestInt(), juce::Justification::centred, false);

        // Fußzeile: Kanal-Kennung L / R unterhalb der LED-Spalten
        auto chRow = inner.removeFromBottom(11.0f);
        inner.reduce(0.0f, 2.0f);

        // Links: 2 LED-Spalten (L/R), Rechts: dBFS-Skala
        auto barsArea  = inner.removeFromLeft(24.0f);
        auto scaleArea = inner;

        g.setColour(kSilverMid.withAlpha(0.85f));
        g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        g.drawText("L", static_cast<int>(barsArea.getX()), static_cast<int>(chRow.getY()), 10, 11, juce::Justification::centred, false);
        g.drawText("R", static_cast<int>(barsArea.getX() + 13.5f), static_cast<int>(chRow.getY()), 10, 11, juce::Justification::centred, false);

        // 24 feine, dünne LED-Striche (Grau unten -> Reinweiß bei 0 dBFS oben)
        constexpr int numSegs = 24;
        const float gapY   = 3.1f;
        const float segH   = std::max(1.5f, (barsArea.getHeight() - gapY * static_cast<float>(numSegs - 1)) / static_cast<float>(numSegs));
        const float colGap = 3.0f;
        const float segW   = (barsArea.getWidth() - colGap) * 0.5f;

        const float levels[2] = { normL, normR };

        for (int ch = 0; ch < 2; ++ch) {
            const float x = barsArea.getX() + static_cast<float>(ch) * (segW + colGap);
            const float activeSegs = levels[ch] * static_cast<float>(numSegs);

            for (int i = 0; i < numSegs; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(numSegs - 1);
                const juce::Colour segCol = kMeterGrayLow.interpolatedWith(kPureWhite, t);

                const float y = barsArea.getBottom() - static_cast<float>(i + 1) * segH - static_cast<float>(i) * gapY;
                auto segRect = juce::Rectangle<float>(x, y, segW, segH);

                const bool isLit = (static_cast<float>(i) + 0.35f) <= activeSegs;
                if (isLit) {
                    g.setColour(segCol.withAlpha(0.22f));
                    g.fillRoundedRectangle(segRect.expanded(0.8f, 0.6f), 1.0f);
                    g.setColour(segCol);
                    g.fillRoundedRectangle(segRect, 0.9f);
                } else {
                    g.setColour(segCol.withAlpha(0.11f));
                    g.fillRoundedRectangle(segRect, 0.9f);
                }
            }
        }

        // dBFS-Skalenstriche & Zahlenwerte (-48 dBFS unten bis 0 dBFS ganz oben)
        struct ScaleMark {
            float db;
            const char* label;
        };
        constexpr ScaleMark marks[] = {
            {   0.0f, "0"   },
            {  -6.0f, "-6"  },
            { -12.0f, "-12" },
            { -24.0f, "-24" },
            { -36.0f, "-36" },
            { -48.0f, "-48" }
        };

        g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        for (const auto& m : marks) {
            const float t = juce::jlimit(0.0f, 1.0f, (m.db + 48.0f) / 48.0f);
            const float y = barsArea.getBottom() - t * barsArea.getHeight();
            const bool isZero = (m.db == 0.0f);

            g.setColour(isZero ? kPureWhite : kSilverMid.withAlpha(0.75f));
            g.drawHorizontalLine(static_cast<int>(std::round(y)), scaleArea.getX() + 2.0f, scaleArea.getX() + 5.5f);

            g.drawText(m.label,
                       static_cast<int>(scaleArea.getX() + 7.0f),
                       static_cast<int>(std::round(y)) - 5,
                       static_cast<int>(scaleArea.getWidth() - 7.0f),
                       10,
                       juce::Justification::centredLeft,
                       false);
        }
    }

    void drawCornerHexScrew(juce::Graphics& g, float cx, float cy) {
        g.setColour(juce::Colours::black.withAlpha(0.65f));
        g.fillEllipse(cx - 5.5f, cy - 4.5f, 11.5f, 11.5f);

        juce::ColourGradient grad(juce::Colour(0xff2c313b), cx - 5.0f, cy - 5.0f,
                                  kChassisBlack,             cx + 5.0f, cy + 5.0f, false);
        g.setGradientFill(grad);
        g.fillEllipse(cx - 5.2f, cy - 5.2f, 10.4f, 10.4f);

        g.setColour(kSilverMid.withAlpha(0.55f));
        g.drawEllipse(cx - 5.2f, cy - 5.2f, 10.4f, 10.4f, 0.9f);

        g.setColour(kSilverDark);
        g.drawLine(cx - 2.4f, cy - 2.4f, cx + 2.4f, cy + 2.4f, 1.2f);
        g.drawLine(cx - 2.4f, cy + 2.4f, cx + 2.4f, cy - 2.4f, 1.2f);
    }
}

MoggLookAndFeel::MoggLookAndFeel() {
    setColour(juce::PopupMenu::backgroundColourId, kModuleDark);
    setColour(juce::PopupMenu::textColourId, kCrispWhite);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kPastelTurquoise.withAlpha(0.28f));
    setColour(juce::PopupMenu::highlightedTextColourId, kCrispWhite);

    setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::textColourId, kCrispWhite);
}

juce::Slider::SliderLayout MoggLookAndFeel::getSliderLayout(juce::Slider& slider) {
    auto layout = LookAndFeel_V4::getSliderLayout(slider);
    const int knobRole = static_cast<int>(slider.getProperties().getWithDefault("knobRole", kRoleStandard));

    if (knobRole == kRoleHero) {
        // Frequenzzahl direkt unter den großen Cutoff-Knob setzen
        const auto local = slider.getLocalBounds();
        const int boxW = 90;
        const int boxH = 19;
        layout.sliderBounds  = local.withTrimmedBottom(26);
        layout.textBoxBounds = juce::Rectangle<int>((local.getWidth() - boxW) / 2,
                                                    layout.sliderBounds.getBottom() - 13,
                                                    boxW,
                                                    boxH);
    }

    return layout;
}

void MoggLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label) {
    const auto outlineCol = label.findColour(juce::Label::outlineColourId);
    const auto bgCol      = label.findColour(juce::Label::backgroundColourId);
    const auto bounds     = label.getLocalBounds().toFloat().reduced(0.5f);
    constexpr float cornerRadius = 4.0f;

    if (!bgCol.isTransparent()) {
        g.setColour(bgCol);
        g.fillRoundedRectangle(bounds, cornerRadius);
    }

    if (!label.isBeingEdited()) {
        const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        const auto font  = getLabelFont(label);

        g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
        g.setFont(font);

        auto textArea = getLabelBorderSize(label).subtractedFrom(label.getLocalBounds());
        g.drawFittedText(label.getText(), textArea, label.getJustificationType(),
                         juce::jmax(1, static_cast<int>(static_cast<float>(textArea.getHeight()) / font.getHeight())),
                         label.getMinimumHorizontalScale());
    }

    if (!outlineCol.isTransparent()) {
        g.setColour(outlineCol.withAlpha(label.isEnabled() ? 0.90f : 0.45f));
        g.drawRoundedRectangle(bounds, cornerRadius, 1.15f);
    }
}

void MoggLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPosProportional, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider& slider) {
    const auto bounds  = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(width), static_cast<float>(height)).reduced(3.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre  = bounds.getCentre();
    const float angle  = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    const auto accent   = slider.findColour(juce::Slider::rotarySliderFillColourId);
    const int knobRole  = static_cast<int>(slider.getProperties().getWithDefault("knobRole", kRoleStandard));
    const bool isHero   = (knobRole == kRoleHero);
    const bool isCenter = (knobRole == kRoleCenter);

    // 1. Leuchtender Radial-Skalenkranz für die beiden großen Hero-Cutoff-Regler
    if (isHero) {
        const float tickOuter = radius - 1.5f;
        const float tickInner = radius - 9.5f;
        constexpr int numTicks = 40;
        for (int i = 0; i <= numTicks; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(numTicks);
            const float a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
            const bool active = (t <= sliderPosProportional + 0.005f);

            const float sinA = std::sin(a);
            const float cosA = -std::cos(a);

            if (active) {
                g.setColour(accent.withAlpha(0.25f));
                g.drawLine(centre.x + sinA * (tickInner - 1.0f),
                           centre.y + cosA * (tickInner - 1.0f),
                           centre.x + sinA * (tickOuter + 1.0f),
                           centre.y + cosA * (tickOuter + 1.0f),
                           3.2f);
                g.setColour(kCrispWhite.interpolatedWith(accent, 0.55f));
            } else {
                g.setColour(accent.withAlpha(0.24f));
            }

            g.drawLine(centre.x + sinA * tickInner,
                       centre.y + cosA * tickInner,
                       centre.x + sinA * tickOuter,
                       centre.y + cosA * tickOuter,
                       1.5f);
        }
    }

    // 2. Vintage-Pastell-Leuchtring um den Regler
    const float arcRadius = radius - (isHero ? 14.5f : (isCenter ? 6.0f : 4.5f));
    const float arcWidth  = isHero ? 4.2f : (isCenter ? 3.5f : 2.6f);

    juce::Path bgArc;
    bgArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(kRecessBlack);
    g.strokePath(bgArc, juce::PathStrokeType(arcWidth + 1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPosProportional > 0.001f) {
        juce::Path valArc;
        valArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle, true);

        if (isCenter) {
            juce::ColourGradient glowGrad(kPastelPink.withAlpha(0.28f), bounds.getX(), centre.y,
                                          kPastelTurquoise.withAlpha(0.28f), bounds.getRight(), centre.y, false);
            g.setGradientFill(glowGrad);
            g.strokePath(valArc, juce::PathStrokeType(arcWidth + 4.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::ColourGradient arcGrad(kPastelPink, bounds.getX(), centre.y,
                                         kPastelTurquoise, bounds.getRight(), centre.y, false);
            g.setGradientFill(arcGrad);
            g.strokePath(valArc, juce::PathStrokeType(arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        } else {
            g.setColour(accent.withAlpha(isHero ? 0.30f : 0.22f));
            g.strokePath(valArc, juce::PathStrokeType(arcWidth + (isHero ? 6.5f : 4.0f),
                                                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(accent);
            g.strokePath(valArc, juce::PathStrokeType(arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // 3. Geriffelter schwarzer Moog-Drehkörper
    const float bodyRadius = radius - (isHero ? 21.0f : (isCenter ? 11.0f : 8.5f));
    if (bodyRadius <= 6.0f) return;

    g.setColour(juce::Colours::black.withAlpha(0.75f));
    g.fillEllipse(centre.x - bodyRadius + 2.0f, centre.y - bodyRadius + 4.0f, bodyRadius * 2.0f, bodyRadius * 2.0f);

    const int numFlutes = isHero ? 24 : 16;
    juce::Path flutedBody;
    const int steps = numFlutes * 4;
    for (int i = 0; i <= steps; ++i) {
        const float theta = (static_cast<float>(i) / static_cast<float>(steps)) * juce::MathConstants<float>::twoPi + angle;
        const float fluteWave = std::cos(static_cast<float>(numFlutes) * (theta - angle));
        const float r = bodyRadius - ((isHero ? 1.1f : 0.65f) * (1.0f - fluteWave));
        const float px = centre.x + std::sin(theta) * r;
        const float py = centre.y - std::cos(theta) * r;
        if (i == 0) flutedBody.startNewSubPath(px, py);
        else        flutedBody.lineTo(px, py);
    }
    flutedBody.closeSubPath();

    juce::ColourGradient bodyGrad(juce::Colour(0xff262a33), centre.x - bodyRadius * 0.6f, centre.y - bodyRadius * 0.6f,
                                  juce::Colour(0xff090a0d), centre.x + bodyRadius * 0.7f, centre.y + bodyRadius * 0.7f, true);
    g.setGradientFill(bodyGrad);
    g.fillPath(flutedBody);

    g.setColour(kSilverDark.withAlpha(0.65f));
    g.strokePath(flutedBody, juce::PathStrokeType(0.9f));

    // 4. Dunkle gebürstete Metallkappe mit konischem Lichtreflex
    const float capRadius = bodyRadius * 0.75f;
    juce::ColourGradient capGrad(juce::Colour(0xff2c313c), centre.x - capRadius, centre.y - capRadius,
                                 juce::Colour(0xff111318), centre.x + capRadius, centre.y + capRadius, false);
    g.setGradientFill(capGrad);
    g.fillEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f);

    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path capClip;
        capClip.addEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f);
        g.reduceClipRegion(capClip);

        juce::Path sheen1, sheen2;
        sheen1.startNewSubPath(centre.x, centre.y);
        sheen1.lineTo(centre.x - capRadius * 1.1f, centre.y - capRadius * 0.42f);
        sheen1.lineTo(centre.x - capRadius * 0.42f, centre.y - capRadius * 1.1f);
        sheen1.closeSubPath();

        sheen2.startNewSubPath(centre.x, centre.y);
        sheen2.lineTo(centre.x + capRadius * 1.1f, centre.y + capRadius * 0.42f);
        sheen2.lineTo(centre.x + capRadius * 0.42f, centre.y + capRadius * 1.1f);
        sheen2.closeSubPath();

        g.setColour(kCrispWhite.withAlpha(0.15f));
        g.fillPath(sheen1);
        g.fillPath(sheen2);
    }

    // Metall-Fasenring um die Kappe
    g.setColour(kSilverMid.withAlpha(0.55f));
    g.drawEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f, isHero ? 1.4f : 1.0f);

    // 5. Kontrastreicher Zeigerstrich
    const float sinPtr = std::sin(angle);
    const float cosPtr = -std::cos(angle);
    g.setColour(kCrispWhite);
    g.drawLine(centre.x + sinPtr * (capRadius * 0.25f),
               centre.y + cosPtr * (capRadius * 0.25f),
               centre.x + sinPtr * (capRadius * 0.96f),
               centre.y + cosPtr * (capRadius * 0.96f),
               isHero ? 2.4f : 1.6f);
}

void MoggLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                           const juce::Colour&,
                                           bool shouldDrawButtonAsHighlighted,
                                           bool) {
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const bool isOn = button.getToggleState();

    juce::Colour ledCol = kPastelPink;
    if (button.getButtonText() == "24 dB" || button.getButtonText() == "48 dB") {
        ledCol = kPastelTurquoise;
    }

    g.setColour(isOn ? juce::Colour(0xff1d2028) : (shouldDrawButtonAsHighlighted ? juce::Colour(0xff191c23) : kRecessBlack));
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(isOn ? ledCol.withAlpha(0.85f) : kSilverDark.withAlpha(0.65f));
    g.drawRoundedRectangle(bounds, 4.0f, isOn ? 1.2f : 0.9f);

    const float cx = bounds.getCentreX();
    const float cy = bounds.getBottom() - 6.0f;
    if (isOn) {
        g.setColour(ledCol.withAlpha(0.32f));
        g.fillEllipse(cx - 4.5f, cy - 4.5f, 9.0f, 9.0f);
        g.setColour(ledCol);
    } else {
        g.setColour(kSilverDark.withAlpha(0.45f));
    }
    g.fillEllipse(cx - 2.3f, cy - 2.3f, 4.6f, 4.6f);
}

void MoggLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                     bool, bool) {
    auto textArea = button.getLocalBounds().withTrimmedBottom(8);
    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    g.setColour(button.getToggleState() ? kCrispWhite : kSilverMid);
    g.drawText(button.getButtonText(), textArea, juce::Justification::centred, false);
}

void MoggLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawButtonAsHighlighted, bool) {
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const bool isOn = button.getToggleState();

    g.setColour(isOn ? juce::Colour(0xff1a1d24) : (shouldDrawButtonAsHighlighted ? juce::Colour(0xff171a20) : kRecessBlack));
    g.fillRoundedRectangle(bounds, 4.0f);

    if (isOn) {
        juce::ColourGradient borderGrad(kPastelPink, bounds.getX(), bounds.getY(),
                                        kPastelTurquoise, bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill(borderGrad);
        g.drawRoundedRectangle(bounds, 4.0f, 1.3f);
    } else {
        g.setColour(kSilverDark.withAlpha(0.75f));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }

    auto switchSlot = bounds.removeFromLeft(28.0f).reduced(5.0f, 6.0f);
    g.setColour(kChassisBlack);
    g.fillRoundedRectangle(switchSlot, 2.5f);
    g.setColour(kSilverDark);
    g.drawRoundedRectangle(switchSlot, 2.5f, 0.8f);

    auto thumb = isOn ? switchSlot.removeFromRight(switchSlot.getWidth() * 0.55f)
                      : switchSlot.removeFromLeft(switchSlot.getWidth() * 0.55f);
    g.setColour(isOn ? kPastelPink : kSilverMid);
    g.fillRoundedRectangle(thumb.reduced(1.0f), 2.0f);

    g.setFont(juce::FontOptions(10.8f, juce::Font::bold));
    g.setColour(isOn ? kCrispWhite : kSilverLight);
    g.drawText(button.getButtonText(), bounds.reduced(2.0f, 0.0f), juce::Justification::centredLeft, false);
}

void MoggLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                   int, int, int, int, juce::ComboBox&) {
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);

    g.setColour(kRecessBlack);
    g.fillRoundedRectangle(bounds, 4.0f);

    // Mittelgrauer, abgerundeter Rahmen passend zu allen Zahlenwerten
    g.setColour(kMediumGray.withAlpha(0.90f));
    g.drawRoundedRectangle(bounds, 4.0f, 1.15f);

    const float arrowX = static_cast<float>(width) - 11.0f;
    const float arrowY = static_cast<float>(height) * 0.5f;
    juce::Path arrow;
    arrow.addTriangle(arrowX - 3.0f, arrowY - 1.8f,
                      arrowX + 3.0f, arrowY - 1.8f,
                      arrowX,        arrowY + 2.2f);
    g.setColour(kSilverLight);
    g.fillPath(arrow);
}

// ==============================================================================

MoggDualLadderAudioProcessorEditor::MoggDualLadderAudioProcessorEditor(MoggDualLadderAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&moggLookAndFeel);

    // 1. INPUT & OUTPUT (jeweils 1 Poti + zentriertes Stereo-Meter mit dBFS-Skala)
    setupRotarySlider(inGainSlider,  &inGainLabel,  "IN LEVEL",  kPastelPink,      kRoleStandard, juce::Slider::TextBoxBelow, 74, 17);
    setupRotarySlider(outGainSlider, &outGainLabel, "OUT LEVEL", kPastelTurquoise, kRoleStandard, juce::Slider::TextBoxBelow, 74, 17);

    // 2. HIGHPASS (Hero-Cutoff + HP RES + SATURATION-Gruppe [AMT links neben dem Knob + MODE])
    setupRotarySlider(hpFreqSlider, nullptr, "", kPastelPink, kRoleHero, juce::Slider::TextBoxBelow, 90, 19);
    setupRotarySlider(hpResSlider,  &hpResLabel,    "HP RES", kPastelPink, kRoleStandard, juce::Slider::TextBoxBelow, 72, 17);
    setupRotarySlider(hpSatSlider,  &hpSatAmtLabel, "AMT",    kPastelPink, kRoleStandard, juce::Slider::TextBoxRight, 42, 17);
    hpSatAmtLabel.setJustificationType(juce::Justification::centredRight);
    setupSatModeBox(hpSatModeBox);

    hpSatModeLabel.setText("MODE", juce::dontSendNotification);
    hpSatModeLabel.setJustificationType(juce::Justification::centredLeft);
    hpSatModeLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    hpSatModeLabel.setColour(juce::Label::textColourId, kSilverMid);
    addAndMakeVisible(hpSatModeLabel);

    // 3. MOD (Zentraler DIRT-Regler)
    setupRotarySlider(dirtSlider, &dirtLabel, "DIRT", kPastelTurquoise, kRoleCenter, juce::Slider::TextBoxBelow, 76, 18);

    // 4. LOWPASS (Hero-Cutoff + SATURATION-Gruppe [AMT links neben dem Knob + MODE] + LP RES)
    setupRotarySlider(lpFreqSlider, nullptr, "", kPastelTurquoise, kRoleHero, juce::Slider::TextBoxBelow, 90, 19);
    setupRotarySlider(lpSatSlider,  &lpSatAmtLabel, "AMT",    kPastelTurquoise, kRoleStandard, juce::Slider::TextBoxRight, 42, 17);
    lpSatAmtLabel.setJustificationType(juce::Justification::centredRight);
    setupSatModeBox(lpSatModeBox);
    setupRotarySlider(lpResSlider,  &lpResLabel,    "LP RES", kPastelTurquoise, kRoleStandard, juce::Slider::TextBoxBelow, 72, 17);

    lpSatModeLabel.setText("MODE", juce::dontSendNotification);
    lpSatModeLabel.setJustificationType(juce::Justification::centredLeft);
    lpSatModeLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    lpSatModeLabel.setColour(juce::Label::textColourId, kSilverMid);
    addAndMakeVisible(lpSatModeLabel);

    // 5. Top-Bar Schalter
    linkButton.onStateChange = [this]() { repaint(); };
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

    setSize(1080, 456);
    startTimerHz(30);
}

MoggDualLadderAudioProcessorEditor::~MoggDualLadderAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void MoggDualLadderAudioProcessorEditor::timerCallback() {
    const float rawInL  = linearToMeterNorm(audioProcessor.consumeInputPeakL());
    const float rawInR  = linearToMeterNorm(audioProcessor.consumeInputPeakR());
    const float rawOutL = linearToMeterNorm(audioProcessor.consumeOutputPeakL());
    const float rawOutR = linearToMeterNorm(audioProcessor.consumeOutputPeakR());

    constexpr float decay = 0.82f;
    const float nextInL  = std::max(rawInL,  dispInL  * decay);
    const float nextInR  = std::max(rawInR,  dispInR  * decay);
    const float nextOutL = std::max(rawOutL, dispOutL * decay);
    const float nextOutR = std::max(rawOutR, dispOutR * decay);

    if (std::abs(nextInL - dispInL) > 0.002f || std::abs(nextInR - dispInR) > 0.002f ||
        std::abs(nextOutL - dispOutL) > 0.002f || std::abs(nextOutR - dispOutR) > 0.002f) {
        dispInL  = nextInL;
        dispInR  = nextInR;
        dispOutL = nextOutL;
        dispOutR = nextOutR;
        repaint(inputMeterRect);
        repaint(outputMeterRect);
    }
}

void MoggDualLadderAudioProcessorEditor::setupRotarySlider(juce::Slider& slider, juce::Label* label,
                                                           const juce::String& text, juce::Colour accent,
                                                           int knobRole,
                                                           juce::Slider::TextEntryBoxPosition textBoxPos,
                                                           int textW, int textH) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(textBoxPos, false, textW, textH);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, kRecessBlack);
    slider.setColour(juce::Slider::textBoxOutlineColourId, kMediumGray);
    slider.setColour(juce::Slider::textBoxTextColourId, kCrispWhite);
    slider.setColour(juce::Slider::textBoxHighlightColourId, accent.withAlpha(0.35f));
    slider.getProperties().set("knobRole", knobRole);
    addAndMakeVisible(slider);

    if (label != nullptr) {
        label->setText(text, juce::dontSendNotification);
        label->setJustificationType(juce::Justification::centred);
        label->setFont(juce::FontOptions(10.5f, juce::Font::bold));
        label->setColour(juce::Label::textColourId, kSilverLight);
        addAndMakeVisible(*label);
    }
}

void MoggDualLadderAudioProcessorEditor::setupSatModeBox(juce::ComboBox& box) {
    box.addItem("LADDER", 1);
    box.addItem("WARM",   2);
    box.addItem("FOLD",   3);
    box.addItem("GRIT",   4);
    box.setJustificationType(juce::Justification::centred);
    box.setColour(juce::ComboBox::backgroundColourId, kRecessBlack);
    box.setColour(juce::ComboBox::textColourId, kCrispWhite);
    box.setColour(juce::ComboBox::outlineColourId, kMediumGray);
    box.setColour(juce::ComboBox::arrowColourId, kSilverLight);
    addAndMakeVisible(box);
}

void MoggDualLadderAudioProcessorEditor::paint(juce::Graphics& g) {
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());

    // 1. Matt-schwarze Moog-Frontplatte
    juce::ColourGradient chassisGrad(juce::Colour(0xff14161c), 0.0f, 0.0f,
                                     kChassisBlack,            0.0f, h, false);
    g.setGradientFill(chassisGrad);
    g.fillAll();

    // 2. Gebürstete Silber-Seitenteile links & rechts
    const float cheekW = 20.0f;
    auto drawSilverCheek = [&](float xPos, bool isLeft) {
        juce::ColourGradient metalGrad(kSilverLight, xPos, 0.0f,
                                       kSilverMid,   xPos + cheekW, 0.0f, false);
        g.setGradientFill(metalGrad);
        g.fillRect(xPos, 0.0f, cheekW, h);

        g.setColour(kCrispWhite.withAlpha(0.65f));
        g.drawVerticalLine(static_cast<int>(isLeft ? xPos + cheekW - 1.0f : xPos), 0.0f, h);
        g.setColour(kSilverDark);
        g.drawVerticalLine(static_cast<int>(isLeft ? xPos : xPos + cheekW - 1.0f), 0.0f, h);
    };
    drawSilverCheek(0.0f, true);
    drawSilverCheek(w - cheekW, false);

    // 3. Äußerer Gehäuse-Rahmen & 4 Eckschrauben
    auto frame = juce::Rectangle<float>(cheekW + 8.0f, 8.0f, w - (cheekW + 8.0f) * 2.0f, h - 16.0f);
    g.setColour(kSilverMid.withAlpha(0.45f));
    g.drawRoundedRectangle(frame, 6.0f, 1.2f);

    drawCornerHexScrew(g, frame.getX() + 14.0f,     frame.getY() + 14.0f);
    drawCornerHexScrew(g, frame.getRight() - 14.0f, frame.getY() + 14.0f);
    drawCornerHexScrew(g, frame.getX() + 14.0f,     frame.getBottom() - 14.0f);
    drawCornerHexScrew(g, frame.getRight() - 14.0f, frame.getBottom() - 14.0f);

    // 4. Kopfzeile: "MOGG DUAL LADDER" & "Slope" Beschriftung
    g.setColour(kCrispWhite);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("MOGG DUAL LADDER", static_cast<int>(frame.getX() + 36.0f), 16, 260, 28, juce::Justification::left);

    g.setColour(kSilverLight);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("Slope -", getWidth() - 332, 18, 52, 26, juce::Justification::right);

    // 4b. Dezente Verbindungsstriche vom zentrierten LINK-Button zu HIGHPASS & LOWPASS
    {
        const bool isLinked = linkButton.getToggleState();
        const auto linkBox  = linkButton.getBounds().toFloat();
        const float lineY   = linkBox.getCentreY();
        const float hpDropX = static_cast<float>(hpModuleRect.getRight() - 28);
        const float lpDropX = static_cast<float>(lpModuleRect.getX() + 28);
        const float modTopY = static_cast<float>(hpModuleRect.getY());

        juce::Path leftLinkPath;
        leftLinkPath.startNewSubPath(linkBox.getX(), lineY);
        leftLinkPath.lineTo(hpDropX, lineY);
        leftLinkPath.lineTo(hpDropX, modTopY);

        juce::Path rightLinkPath;
        rightLinkPath.startNewSubPath(linkBox.getRight(), lineY);
        rightLinkPath.lineTo(lpDropX, lineY);
        rightLinkPath.lineTo(lpDropX, modTopY);

        const auto leftCol  = isLinked ? kPastelPink.withAlpha(0.80f)      : kSilverMid.withAlpha(0.35f);
        const auto rightCol = isLinked ? kPastelTurquoise.withAlpha(0.80f) : kSilverMid.withAlpha(0.35f);

        g.setColour(leftCol);
        g.strokePath(leftLinkPath, juce::PathStrokeType(1.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        g.fillEllipse(hpDropX - 2.2f, modTopY - 2.2f, 4.4f, 4.4f);

        g.setColour(rightCol);
        g.strokePath(rightLinkPath, juce::PathStrokeType(1.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        g.fillEllipse(lpDropX - 2.2f, modTopY - 2.2f, 4.4f, 4.4f);
    }

    // 5. Die 5 Modul-Container
    auto drawModuleContainer = [&](juce::Rectangle<int> rect, const juce::String& title,
                                   juce::Colour bannerBg, juce::Colour bannerText,
                                   juce::Colour borderCol) {
        auto r = rect.toFloat();
        g.setColour(kModuleDark);
        g.fillRoundedRectangle(r, 6.0f);

        g.setColour(borderCol.withAlpha(0.85f));
        g.drawRoundedRectangle(r, 6.0f, 1.4f);

        auto banner = r.removeFromTop(22.0f);
        g.setColour(bannerBg);
        g.fillRoundedRectangle(banner.reduced(1.0f, 1.0f), 5.0f);

        g.setColour(bannerText);
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText(title, banner.toNearestInt(), juce::Justification::centred, false);
    };

    drawModuleContainer(inputModuleRect,  "INPUT",    kCrispWhite,      kChassisBlack, kCrispWhite.withAlpha(0.75f));
    drawModuleContainer(hpModuleRect,     "HIGHPASS", kPastelPink,      kChassisBlack, kPastelPink);
    drawModuleContainer(modModuleRect,    "MOD",      kCrispWhite,      kChassisBlack, kCrispWhite.withAlpha(0.75f));
    drawModuleContainer(lpModuleRect,     "LOWPASS",  kPastelTurquoise, kChassisBlack, kPastelTurquoise);
    drawModuleContainer(outputModuleRect, "OUTPUT",   kCrispWhite,      kChassisBlack, kCrispWhite.withAlpha(0.75f));

    // 6. Plakative "CUTOFF" Überschriften über den beiden großen Hero-Reglern
    auto drawCutoffHeading = [&](juce::Rectangle<int> moduleRect, juce::Colour pastelCol) {
        auto titleBox = juce::Rectangle<float>(static_cast<float>(moduleRect.getX()),
                                               static_cast<float>(moduleRect.getY() + 25),
                                               static_cast<float>(moduleRect.getWidth()),
                                               32.0f);
        g.setColour(pastelCol.withAlpha(0.24f));
        g.setFont(juce::FontOptions(26.0f, juce::Font::bold));
        g.drawText("CUTOFF", titleBox.translated(0.0f, 1.0f).toNearestInt(), juce::Justification::centred, false);

        g.setColour(pastelCol);
        g.drawText("CUTOFF", titleBox.toNearestInt(), juce::Justification::centred, false);
    };
    drawCutoffHeading(hpModuleRect, kPastelPink);
    drawCutoffHeading(lpModuleRect, kPastelTurquoise);

    // 7. SATURATION-Subboxen (AMT links neben vergrößertem Knob + MODE darunter)
    auto drawSatGroupBox = [&](juce::Rectangle<int> boxRect, juce::Colour pastelCol) {
        auto r = boxRect.toFloat();
        g.setColour(kRecessBlack.withAlpha(0.85f));
        g.fillRoundedRectangle(r, 5.0f);

        g.setColour(pastelCol.withAlpha(0.8f));
        g.drawRoundedRectangle(r, 5.0f, 1.2f);

        auto header = r.removeFromTop(17.0f);
        g.setColour(pastelCol);
        g.fillRoundedRectangle(header.reduced(0.5f, 0.5f), 4.0f);

        g.setColour(kChassisBlack);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("SATURATION", header.toNearestInt(), juce::Justification::centred, false);
    };
    drawSatGroupBox(hpSatBoxRect, kPastelPink);
    drawSatGroupBox(lpSatBoxRect, kPastelTurquoise);

    // 8. Live Stereo-LED-Meter (Grau bis Reinweiß bei 0 dBFS) in INPUT & OUTPUT
    drawStereoLedMeter(g, inputMeterRect,  dispInL,  dispInR);
    drawStereoLedMeter(g, outputMeterRect, dispOutL, dispOutR);
}

void MoggDualLadderAudioProcessorEditor::resized() {
    const int cheekW = 20;
    auto frame = getLocalBounds().reduced(cheekW + 8, 8);

    // Kopfzeile: Slope-Taster rechts, LINK FREQ exakt zentriert über MOD
    auto topBar = frame.reduced(34, 0).removeFromTop(44);
    topBar.removeFromTop(6);

    const int slopeTotalW = 216;
    auto slopeArea = topBar.removeFromRight(slopeTotalW).reduced(0, 2);
    const int slopeBtnW = slopeTotalW / 4;
    for (size_t i = 0; i < slopeButtons.size(); ++i) {
        slopeButtons[i].setBounds(slopeArea.removeFromLeft(slopeBtnW).reduced(2, 0));
    }

    // Haupt-Deck mit 5 Modulen
    auto deck = frame.reduced(12, 0);
    deck.setTop(62);
    deck.setBottom(frame.getBottom() - 12);

    constexpr int gap = 10;
    const int sideW   = 114;
    const int modW    = 134;
    const int filterW = (deck.getWidth() - sideW * 2 - modW - gap * 4) / 2;

    inputModuleRect  = deck.removeFromLeft(sideW);
    deck.removeFromLeft(gap);
    hpModuleRect     = deck.removeFromLeft(filterW);
    deck.removeFromLeft(gap);
    modModuleRect    = deck.removeFromLeft(modW);
    deck.removeFromLeft(gap);
    lpModuleRect     = deck.removeFromLeft(filterW);
    deck.removeFromLeft(gap);
    outputModuleRect = deck;

    // LINK FREQ exakt mittig über dem MOD-Modul platzieren
    const int linkW = 114;
    const int linkH = 26;
    linkButton.setBounds(modModuleRect.getCentreX() - linkW / 2, topBar.getY() + 5, linkW, linkH);

    // 1. INPUT-Modul: oben 1 Poti (IN LEVEL), unten zentriertes Stereo-LED-Meter
    {
        auto inner = inputModuleRect.reduced(8, 0);
        inner.removeFromTop(27);
        inner.removeFromBottom(10);

        inGainLabel.setBounds(inner.removeFromTop(16));
        inGainSlider.setBounds(inner.removeFromTop(94));
        inner.removeFromTop(6);
        inputMeterRect = inner.withSizeKeepingCentre(64, inner.getHeight());
    }

    // 2. OUTPUT-Modul: oben zentriertes Stereo-LED-Meter, unten 1 Poti (OUT LEVEL)
    {
        auto inner = outputModuleRect.reduced(8, 0);
        inner.removeFromTop(27);
        inner.removeFromBottom(10);

        outGainSlider.setBounds(inner.removeFromBottom(94));
        outGainLabel.setBounds(inner.removeFromBottom(16));
        inner.removeFromBottom(6);
        outputMeterRect = inner.withSizeKeepingCentre(64, inner.getHeight());
    }

    // 3. MOD-Modul: zentraler DIRT-Regler
    {
        auto inner = modModuleRect.reduced(8, 0);
        inner.removeFromTop(28);
        auto centerBlock = inner.withSizeKeepingCentre(inner.getWidth(), 144);
        dirtLabel.setBounds(centerBlock.removeFromTop(18));
        dirtSlider.setBounds(centerBlock);
    }

    // Hilfsfunktion für die SATURATION-Subbox: AMT links neben dem größeren Knob, MODE darunter
    auto layoutSatSubBox = [](juce::Rectangle<int> box,
                              juce::Slider& satSlider, juce::Label& amtLabel,
                              juce::Label& modeLabel, juce::ComboBox& modeBox) {
        auto inner = box.reduced(6, 0);
        inner.removeFromTop(19);
        inner.removeFromBottom(6);

        auto bottomRow = inner.removeFromBottom(22);
        modeLabel.setBounds(bottomRow.removeFromLeft(36));
        modeBox.setBounds(bottomRow);

        inner.removeFromBottom(4);

        amtLabel.setBounds(inner.removeFromLeft(30));
        inner.removeFromLeft(2);
        satSlider.setBounds(inner);
    };

    // 4. HIGHPASS-Modul: riesiger CUTOFF oben, unten HP RES (links) + SATURATION-Box (rechts)
    {
        auto inner = hpModuleRect.reduced(10, 0);
        inner.removeFromTop(58);
        inner.removeFromBottom(8);

        auto bottomRow = inner.removeFromBottom(120);
        auto resCol = bottomRow.removeFromLeft(102);
        bottomRow.removeFromLeft(8);
        hpSatBoxRect = bottomRow;

        hpResLabel.setBounds(resCol.removeFromTop(16));
        hpResSlider.setBounds(resCol);

        layoutSatSubBox(hpSatBoxRect, hpSatSlider, hpSatAmtLabel, hpSatModeLabel, hpSatModeBox);

        inner.removeFromBottom(4);
        hpFreqSlider.setBounds(inner.withSizeKeepingCentre(std::min(inner.getWidth(), 204), inner.getHeight()));
    }

    // 5. LOWPASS-Modul: riesiger CUTOFF oben, unten SATURATION-Box (links) + LP RES (rechts)
    {
        auto inner = lpModuleRect.reduced(10, 0);
        inner.removeFromTop(58);
        inner.removeFromBottom(8);

        auto bottomRow = inner.removeFromBottom(120);
        auto resCol = bottomRow.removeFromRight(102);
        bottomRow.removeFromRight(8);
        lpSatBoxRect = bottomRow;

        lpResLabel.setBounds(resCol.removeFromTop(16));
        lpResSlider.setBounds(resCol);

        layoutSatSubBox(lpSatBoxRect, lpSatSlider, lpSatAmtLabel, lpSatModeLabel, lpSatModeBox);

        inner.removeFromBottom(4);
        lpFreqSlider.setBounds(inner.withSizeKeepingCentre(std::min(inner.getWidth(), 204), inner.getHeight()));
    }
}
