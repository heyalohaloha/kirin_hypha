#include "HyphaSpectrumComponent.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hypha
{
namespace
{
    constexpr float kDisplayRangeDb = KIRIN_SPECTRUM_DISPLAY_RANGE_DB;

    bool validSnapshot (const KirinSpectrumView& view) noexcept
    {
        if (view.has_data == 0)
            return false;
        if (view.status != KIRIN_SPECTRUM_ACTIVE
            || view.sample_rate < 8000u
            || ! std::isfinite (view.min_hz)
            || ! std::isfinite (view.max_hz)
            || view.max_hz <= view.min_hz)
            return false;
        return std::all_of (std::begin (view.display_db), std::end (view.display_db),
                            [] (float value) { return std::isfinite (value); });
    }
}

SpectrumComponent::SpectrumComponent()
{
    setInterceptsMouseClicks (false, false);
    setAccessible (false);
}

void SpectrumComponent::setSnapshot (const KirinSpectrumView& next)
{
    if (haveSnapshot && std::memcmp (&snapshot, &next, sizeof (snapshot)) == 0)
        return;
    snapshot = next;
    haveSnapshot = true;
    repaint();
}

float SpectrumComponent::yForDb (float db, juce::Rectangle<float> plot) noexcept
{
    const float clipped = juce::jlimit (-kDisplayRangeDb, kDisplayRangeDb, db);
    return juce::jmap (clipped, kDisplayRangeDb, -kDisplayRangeDb,
                       plot.getY(), plot.getBottom());
}

juce::String SpectrumComponent::statusText (uint8_t status)
{
    if (status == KIRIN_SPECTRUM_NO_PAIR) return juce::CharPointer_UTF8 ("PAIR —");
    if (status == KIRIN_SPECTRUM_WARMING_UP) return juce::CharPointer_UTF8 ("SYNC ◌");
    if (status == KIRIN_SPECTRUM_UNAVAILABLE) return juce::CharPointer_UTF8 ("DATA —");
    return {};
}

void SpectrumComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto plot = bounds.reduced (22.0f, 8.0f).withTrimmedBottom (8.0f);

    g.setFont (monoFont (9.0f));
    g.setColour (COL_MUTED);
    g.drawText ("+18", 0, juce::roundToInt (plot.getY()) - 4, 20, 10,
                juce::Justification::centredRight);
    g.drawText ("0", 0, juce::roundToInt (plot.getCentreY()) - 5, 20, 10,
                juce::Justification::centredRight);
    g.drawText ("-18", 0, juce::roundToInt (plot.getBottom()) - 6, 20, 10,
                juce::Justification::centredRight);

    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float y = yForDb (db, plot);
        g.setColour ((db == 0.0f ? COL_FLORA : COL_MUTED).withAlpha (db == 0.0f ? 0.42f : 0.22f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }

    g.setColour (COL_MUTED);
    g.drawText ("10", juce::roundToInt (plot.getX()), juce::roundToInt (plot.getBottom()),
                30, 10, juce::Justification::centredLeft);
    g.drawText ("1k", juce::roundToInt (plot.getCentreX()) - 15,
                juce::roundToInt (plot.getBottom()), 30, 10, juce::Justification::centred);
    g.drawText ("22k", juce::roundToInt (plot.getRight()) - 30,
                juce::roundToInt (plot.getBottom()), 30, 10,
                juce::Justification::centredRight);

    if (! haveSnapshot || ! validSnapshot (snapshot))
    {
        const auto text = haveSnapshot ? statusText (snapshot.status)
                                       : juce::String ("SYNC");
        if (text.isNotEmpty())
        {
            g.setColour (COL_MUTED);
            g.setFont (monoFont (13.0f));
            g.drawText (text, plot, juce::Justification::centred);
        }
        return;
    }

    juce::Path curve;
    for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        const float x = juce::jmap (static_cast<float> (index), 0.0f,
                                    static_cast<float> (KIRIN_SPECTRUM_BAND_COUNT - 1u),
                                    plot.getX(), plot.getRight());
        const float y = yForDb (snapshot.display_db[index], plot);
        if (index == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }
    g.setColour (COL_NORMAL.withAlpha (0.92f));
    g.strokePath (curve, juce::PathStrokeType (1.2f));
}
}
