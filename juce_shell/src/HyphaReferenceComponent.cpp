#include "HyphaReferenceComponent.h"

#include "HyphaTheme.h"

#include <utility>

namespace hypha::reference_ui
{
namespace
{
juce::String valueText (double value, bool delta)
{
    return delta ? fmtDelta (value) : fmtVal (value);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> area, float alpha = 0.66f)
{
    g.setColour (BG.withAlpha (alpha));
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 0.8f);
}

void drawComparisonRoots (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto field = area.reduced (18.0f, 28.0f);
    for (int strand = 0; strand < 3; ++strand)
    {
        const float offset = (static_cast<float> (strand) - 1.0f) * field.getHeight() * 0.08f;
        juce::Path root;
        root.startNewSubPath (field.getX(), field.getCentreY() + offset);
        root.cubicTo (field.getX() + field.getWidth() * 0.28f,
                      field.getCentreY() - offset * 1.4f,
                      field.getX() + field.getWidth() * 0.70f,
                      field.getCentreY() + offset * 1.6f,
                      field.getRight(), field.getCentreY() - offset);
        g.setColour ((strand == 1 ? COL_SPECTRUM_POST : COL_FLORA)
                         .withAlpha (strand == 1 ? 0.075f : 0.045f));
        g.strokePath (root, juce::PathStrokeType (0.55f + strand * 0.18f));
    }
}

void drawValue (juce::Graphics& g, juce::Rectangle<float> area,
                const juce::String& heading, double value, const juce::String& unit,
                juce::Colour colour, bool delta, float scale)
{
    auto label = area.removeFromTop (14.0f * scale);
    g.setColour (COL_MUTED.withAlpha (0.92f));
    g.setFont (labelFont (8.0f * scale));
    g.drawFittedText (heading, label.toNearestInt(), juce::Justification::centred, 1, 0.76f);
    auto unitArea = area.removeFromBottom (12.0f * scale);
    g.setColour (COL_MUTED.withAlpha (0.84f));
    g.setFont (labelFont (7.5f * scale));
    g.drawText (unit, unitArea, juce::Justification::centred);
    g.setColour (std::isfinite (value) ? colour : COL_MUTED);
    drawTabularText (g, monoFont (18.0f * scale), valueText (value, delta), area,
                     juce::Justification::centred);
}

void drawMetric (juce::Graphics& g, juce::Rectangle<float> area,
                 const juce::String& name, const juce::String& unit,
                 double a, double b, double delta)
{
    drawPanel (g, area);
    drawComparisonRoots (g, area);
    const float scale = juce::jlimit (1.0f, 2.2f, area.getHeight() / 170.0f);
    auto header = area.removeFromTop (18.0f * scale);
    g.setColour (COL_NORMAL.withAlpha (0.78f));
    g.setFont (labelFont (9.0f * scale));
    g.drawText (name, header.reduced (9.0f, 0.0f), juce::Justification::centredLeft);
    area.reduce (5.0f, 3.0f);
    const float columnWidth = area.getWidth() / 3.0f;
    drawValue (g, area.removeFromLeft (columnWidth), "A", a, unit,
               COL_OBSERVATORY_VALUE, false, scale);
    drawValue (g, area.removeFromLeft (columnWidth), "B", b, unit,
               COL_OBSERVATORY_VALUE, false, scale);
    drawValue (g, area, "B-A", delta, unit == "LUFS" ? "LU" : "dB",
               COL_SPECTRUM_DELTA_BR, true, scale * 1.12f);
}

void drawCompactDelta (juce::Graphics& g, juce::Rectangle<float> area,
                       const juce::String& name, double value, const juce::String& unit)
{
    drawPanel (g, area, 0.72f);
    area.reduce (4.0f, 3.0f);
    drawValue (g, area, "B-A  " + name, value, unit,
               COL_SPECTRUM_DELTA_BR, true, 1.0f);
}
}

Component::SideButton::SideButton (const juce::String& text) : juce::TextButton (text)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Component::SideButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    const bool selected = getToggleState();
    g.setColour ((selected ? COL_SPECTRUM_POST : kFieldFill)
                     .withAlpha (selected ? 0.24f : highlighted ? 0.86f : 0.62f));
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour ((selected ? COL_SPECTRUM_DELTA_BR : COL_MUTED)
                     .withAlpha (isEnabled() ? (down ? 1.0f : 0.82f) : 0.28f));
    g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, selected ? 1.2f : 0.7f);
    g.setColour (! isEnabled() ? COL_MUTED.withAlpha (0.32f)
                               : selected ? COL_OBSERVATORY_VALUE : COL_NORMAL);
    g.setFont (labelFont (juce::jlimit (10.0f, 16.0f, getHeight() * 0.42f)));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
}

Component::Component()
{
    setOpaque (false);
    aButton.setComponentID ("reference-a");
    bButton.setComponentID ("reference-b");
    aButton.setTitle ("Audition A");
    bButton.setTitle ("Audition B");
    aButton.setTooltip ("Return to the live DAW mix (A).");
    bButton.setTooltip ("Audition the Kirin OS prepared Reference (B).");
    aButton.onClick = [this] { if (onSelectA) onSelectA(); };
    bButton.onClick = [this] { if (onSelectB) onSelectB(); };
    addAndMakeVisible (aButton);
    addAndMakeVisible (bButton);
}

void Component::setState (State next)
{
    current = std::move (next);
    aButton.setToggleState (! current.bSelected, juce::dontSendNotification);
    bButton.setToggleState (current.bSelected, juce::dontSendNotification);
    bButton.setEnabled (canSelectB (current));
    repaint();
}

bool Component::detailedLayout() const noexcept
{
    return getWidth() >= 520 && getHeight() >= 180;
}

void Component::resized()
{
    auto area = getLocalBounds().reduced (6);
    auto header = area.removeFromTop (detailedLayout() ? 42 : 34);
    const int buttonWidth = detailedLayout() ? 62 : 48;
    bButton.setBounds (header.removeFromRight (buttonWidth));
    header.removeFromRight (3);
    aButton.setBounds (header.removeFromRight (buttonWidth));
}

void Component::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().reduced (6);
    auto header = area.removeFromTop (detailedLayout() ? 42 : 34);
    header.removeFromRight ((detailedLayout() ? 62 : 48) * 2 + 3);
    const auto source = current.sourceLabel.isNotEmpty() ? current.sourceLabel : "KIRIN OS";
    g.setColour (COL_FLORA.withAlpha (0.86f));
    g.setFont (labelFont (detailedLayout() ? 10.5f : 8.5f));
    g.drawFittedText ("REFERENCE / " + source, header.removeFromTop (14),
                      juce::Justification::centredLeft, 1, 0.72f);
    g.setColour (COL_OBSERVATORY_VALUE);
    g.setFont (labelFont (detailedLayout() ? 16.0f : 12.0f));
    g.drawFittedText (current.title.isNotEmpty() ? current.title : "OPEN IN HYPHA",
                      header, juce::Justification::centredLeft, 1, 0.68f);

    area.removeFromTop (4);
    auto statusArea = area.removeFromBottom (detailedLayout() ? 24 : 18);
    const auto statusColour = current.readiness == Readiness::rejected
        ? COL_LED_YELLOW : current.bSelected ? COL_SPECTRUM_DELTA_BR : COL_MUTED;
    g.setColour (statusColour.withAlpha (0.92f));
    g.setFont (labelFont (detailedLayout() ? 11.0f : 8.5f));
    auto statusText = current.status;
    if (detailedLayout() && current.alignmentLabel.isNotEmpty())
        statusText += (statusText.isNotEmpty() ? "  /  " : "") + current.alignmentLabel;
    auto primaryStatusArea = statusArea;
    if (detailedLayout() && current.bSelected)
        primaryStatusArea = statusArea.removeFromLeft (
            juce::roundToInt (statusArea.getWidth() * 0.52f));
    g.drawFittedText (statusText, primaryStatusArea.reduced (4, 0),
                      juce::Justification::centredLeft, 1, 0.74f);

    if (detailedLayout())
    {
        auto metrics = area;
        const float gap = 6.0f;
        const float width = (metrics.getWidth() - gap) * 0.5f;
        drawMetric (g, metrics.removeFromLeft (juce::roundToInt (width)).toFloat(),
                    "INTEGRATED LOUDNESS", "LUFS", current.aIntegratedLoudness,
                    current.adjustedBIntegratedLoudness, current.loudnessDeltaBMinusA);
        metrics.removeFromLeft (juce::roundToInt (gap));
        drawMetric (g, metrics.toFloat(), "MAXIMUM TRUE PEAK", "dBTP",
                    current.aMaximumTruePeakDbtp, current.adjustedBMaximumTruePeakDbtp,
                    current.truePeakDeltaBMinusA);
        if (current.bSelected && std::isfinite (current.appliedGainDb))
        {
            const auto gain = "B GAIN " + fmtDelta (current.appliedGainDb) + " dB  /  "
                + (current.gainLimited ? "MATCH LIMITED" : "LOUDNESS MATCHED")
                + "  /  TP LIMIT -1.0 dBTP";
            g.setColour ((current.gainLimited ? COL_FLORA_BR : COL_MUTED).withAlpha (0.9f));
            g.setFont (labelFont (10.0f));
            g.drawFittedText (gain, statusArea.reduced (4, 0),
                              juce::Justification::centredRight, 1, 0.65f);
        }
    }
    else
    {
        const int gap = 4;
        auto left = area.removeFromLeft ((area.getWidth() - gap) / 2);
        area.removeFromLeft (gap);
        drawCompactDelta (g, left.toFloat(), "LUFS-I", current.loudnessDeltaBMinusA, "LU");
        drawCompactDelta (g, area.toFloat(), "MAX TP", current.truePeakDeltaBMinusA, "dB");
    }
}
}
