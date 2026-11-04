#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "HyphaSpectrumPeaks.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

namespace hypha::spectrum_peaks
{
// The free height nearest the peak, above it first. That height either sits at the peak or
// touches a plate already placed, so those are the only heights worth trying.
inline juce::Rectangle<float> freeRow (juce::Rectangle<float> plot, juce::Rectangle<float> tag,
                                       float y, float gap,
                                       const juce::Rectangle<float>* placed, std::size_t count)
{
    const float above = y - gap - tag.getHeight();
    std::vector<float> tops { above, y + gap };
    for (std::size_t index = 0; index < count; ++index)
    {
        tops.push_back (placed[index].getY() - tag.getHeight() - 1.0f);
        tops.push_back (placed[index].getBottom() + 1.0f);
    }
    std::stable_sort (tops.begin(), tops.end(), [above] (float left, float right)
    {
        return std::abs (left - above) < std::abs (right - above);
    });
    for (const float top : tops)
    {
        const auto row = tag.withY (top);
        if (row.getY() >= plot.getY() && row.getBottom() <= plot.getBottom()
            && std::none_of (placed, placed + count,
                             [&] (const auto& other) { return other.intersects (row); }))
            return row;
    }
    return tag.constrainedWithin (plot);
}

// A small plate over each peak, joined to the curve. A tag that would cover another moves to the
// next free row. `yFor` keeps it on the curve as drawn, and `textFor` says what the chart can
// honestly name at that peak.
template <typename XFor, typename YFor, typename TextFor>
void paintTags (juce::Graphics& g, juce::Rectangle<float> plot, float scale, const Peaks& peaks,
                XFor&& xFor, YFor&& yFor, TextFor&& textFor)
{
    const float height = 8.5f * scale;
    const float gap = 2.0f * scale;
    std::array<juce::Rectangle<float>, 3> placed {};
    for (std::size_t index = 0; index < peaks.count; ++index)
    {
        const auto& peak = peaks.found[index];
        const juce::String text = textFor (peak);
        const float width = text_style::shownWidth (g.getCurrentFont(), text) + 8.0f * scale;
        const float x = xFor (peak.hz);
        const float y = yFor (peak);
        const auto wanted = juce::Rectangle<float> (x - width * 0.5f, y - gap - height, width, height)
                                .constrainedWithin (plot);
        const auto tag = freeRow (plot, wanted, y, gap, placed.data(), index);
        placed[index] = tag;
        g.setColour (COL_NORMAL.withAlpha (0.42f));
        g.drawLine (x, tag.getCentreY() < y ? tag.getBottom() : tag.getY(), x, y, 0.75f * scale);
        g.setColour (BG.withAlpha (0.78f));
        g.fillRoundedRectangle (tag, 2.5f * scale);
        g.setColour (COL_NORMAL.withAlpha (0.94f));
        text_style::drawText (g, text, tag.toNearestInt(), juce::Justification::centred);
    }
}
}
