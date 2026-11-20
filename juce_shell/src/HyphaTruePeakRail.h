#pragma once

#include "HyphaHybridVuPainter.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

#include <array>
#include <cmath>
#include <vector>

namespace hypha::hybrid_vu::peak_rail
{
inline float scaled (float value, float width, float minimum = 0.7f) noexcept
{
    return juce::jmax (minimum, value * width / 900.0f);
}

inline juce::Point<float> point (juce::Rectangle<float> face, int channel, float amount)
{
    const auto inner = channel == 0 ? 0.384f : 0.616f;
    const auto outer = channel == 0 ? 0.071f : 0.929f;
    return { face.getX() + face.getWidth() * juce::jmap (amount, outer, inner),
             face.getY() + face.getHeight()
                 * (0.225f - 0.060f * std::sin (amount * juce::MathConstants<float>::pi * 0.5f)) };
}

// The sample-clip lamp is unchanged. This colour marks only the rail's part above 0 dBTP.
inline juce::Colour colour (double dbtp, bool held = false) noexcept
{
    const auto over = COL_FLORA_BR.interpolatedWith (COL_NORMAL, 0.55f);
    return dbtp > 0.0 ? over : held ? COL_FLORA_BR : COL_SPECTRUM_DELTA;
}

struct Label { juce::String text; juce::Rectangle<float> area; };

inline juce::Font labelFontFor (presentation::Context context)
{
    context.referenceCompact = true;
    return monoFont (context, typography::TextRole::axis, typography::Composition::instrument);
}

inline std::vector<Label> labels (juce::Rectangle<float> face, int channel,
                                  const presentation::Context& context)
{
    const auto font = labelFontFor (context);
    std::vector<Label> placed;
    // Keep both ends and zero first. The +3 label sits below the rail, not against the zero.
    for (const auto value : std::array<int, 6> { 3, 0, -24, -18, -12, -6 })
    {
        const auto at = point (face, channel, truePeakNormalized (value));
        const auto text = value > 0 ? "+" + juce::String (value) : juce::String (value);
        const auto width = std::ceil (tabularTextWidth (font, text)) + 2.0f;
        const auto y = value == 3 ? at.y + scaled (16.0f, face.getWidth(), 6.0f)
                                  : at.y - scaled (28.0f, face.getWidth(), font.getHeight() + 3.0f);
        const juce::Rectangle<float> area (at.x - width * 0.5f, y, width, font.getHeight());
        bool free = face.contains (area);
        for (const auto& other : placed) free = free && ! other.area.expanded (2.0f, 0.0f).intersects (area);
        if (free) placed.push_back ({ text, area });
    }
    return placed;
}

inline void paint (juce::Graphics& g, juce::Rectangle<float> face, const State& state,
                   int channel, bool compact)
{
    const auto available = state.currentAvailable && channel < state.meter.channels
        && std::isfinite (state.meter.channel_instant_true_peak_dbtp[channel]);
    const auto progress = available ? truePeakNormalized (state.meter.channel_instant_true_peak_dbtp[channel]) : 0.0f;
    const int segments = compact ? 21 : 49;
    const auto width = face.getWidth();
    for (int index = 0; index < segments; ++index)
    {
        const auto amount = static_cast<float> (index) / static_cast<float> (segments - 1);
        const auto at = point (face, channel, amount);
        const auto emphasized = available && amount <= progress && amount >= truePeakNormalized (-24.0);
        const auto height = scaled (index % (compact ? 4 : 7) == 0 ? 12.0f : 9.0f,
                                    width, compact ? 2.4f : 3.5f);
        const auto tint = colour (amount * 31.0 - 28.0);
        if (emphasized)
        {
            const auto segmentWidth = scaled (compact ? 4.0f : 5.6f, width, 1.3f);
            g.setColour (tint.withAlpha (0.14f));
            g.fillRoundedRectangle (at.x - segmentWidth, at.y - height * 0.75f,
                                    segmentWidth * 2.0f, height * 1.5f, segmentWidth * 0.55f);
            g.setColour (tint.withAlpha (0.96f));
            g.fillRoundedRectangle (at.x - segmentWidth * 0.5f, at.y - height * 0.5f,
                                    segmentWidth, height, segmentWidth * 0.28f);
        }
        else
        {
            g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.78f));
            g.drawVerticalLine (juce::roundToInt (at.x), at.y - height * 0.5f, at.y + height * 0.5f);
        }
    }
    // Show a small positive overshoot even between the rail's discrete segments.
    if (available && state.meter.channel_instant_true_peak_dbtp[channel] > 0.0)
    {
        const auto at = point (face, channel, progress);
        const auto height = scaled (9.0f, width, 2.4f);
        g.setColour (colour (state.meter.channel_instant_true_peak_dbtp[channel]));
        g.fillRect (juce::Rectangle<float> (at.x - 0.7f, at.y - height * 0.5f, 1.4f, height));
    }
    // Exact unlabelled 1 and 2 dBTP ticks, independent of the decorative segment spacing.
    for (const auto value : { 1.0, 2.0 })
    {
        const auto at = point (face, channel, truePeakNormalized (value));
        g.setColour (COL_NORMAL);
        g.drawVerticalLine (juce::roundToInt (at.x), at.y + scaled (7.0f, width, 2.5f),
                            at.y + scaled (12.0f, width, 4.0f));
    }
    if (state.cumulativeAvailable && channel < state.meter.channels
        && std::isfinite (state.meter.channel_max_true_peak_dbtp[channel]))
    {
        const auto value = state.meter.channel_max_true_peak_dbtp[channel];
        const auto at = point (face, channel, truePeakNormalized (value));
        const auto tint = colour (value, true);
        g.setColour (tint.withAlpha (0.17f));
        g.fillRoundedRectangle (at.x - scaled (5.0f, width), at.y - scaled (15.0f, width),
                                scaled (10.0f, width), scaled (30.0f, width), scaled (4.0f, width));
        g.setColour (tint);
        g.fillRoundedRectangle (at.x - scaled (2.0f, width), at.y - scaled (12.0f, width),
                                scaled (4.0f, width), scaled (24.0f, width), scaled (1.5f, width));
    }
    g.setColour (COL_NORMAL);
    const auto font = labelFontFor (state.presentation);
    for (const auto& label : labels (face, channel, state.presentation))
        drawTabularText (g, font, label.text, label.area, juce::Justification::centred);
}
}
