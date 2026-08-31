#include "HyphaObservatoryView.h"

#include <cmath>
#include <string>

namespace hypha::observatory
{
namespace
{
juce::String clipCountText (uint64_t value)
{
    return juce::String (std::to_string (value));
}

void paintClipCount (juce::Graphics& g,
                     juce::Rectangle<int> area,
                     const char* channel,
                     uint64_t count,
                     bool available)
{
    g.setColour (available && count > 0 ? COL_FLORA_BR : COL_MUTED);
    g.setFont (monoFont (area.getHeight() < 14 ? 7.0f : 8.0f));
    const auto value = available ? clipCountText (count) : juce::String ("—");
    drawTabularText (g, monoFont (area.getHeight() < 14 ? 7.0f : 8.0f),
                     juce::String (channel) + " " + value,
                     area.toFloat(), juce::Justification::centred);
}
}

SizePreset View::currentPreset() const noexcept
{
    for (const auto preset : sizePresets)
        if (preset.width == getWidth() && preset.height == getHeight())
            return preset;
    const auto density = getWidth() < 338 ? Density::compact
                       : getWidth() < 413 ? Density::focused
                       : getWidth() < 525 ? Density::standard : Density::observatory;
    return { getWidth(), getHeight(), density, "SIZE" };
}

void View::cycleSize()
{
    const auto preset = currentPreset();
    size_t current = 0;
    for (size_t index = 0; index < sizePresets.size(); ++index)
        if (sizePresets[index].width == preset.width)
            current = index;
    if (onSizeChange)
        onSizeChange (sizePresets[(current + 1u) % sizePresets.size()]);
}

void View::paintChannelStrips (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& meter = observatoryFrame.meter;
    const bool currentAvailable = currentFactsAvailable();
    const bool cumulativeAvailable = cumulativeFactsAvailable();
    g.setColour (BG.withAlpha (0.82f));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);
    area.reduce (5, 5);
    auto labels = area.removeFromTop (15);
    auto clips = area.removeFromBottom (23);
    const auto columnGap = 4;
    const auto columnWidth = (area.getWidth() - columnGap) / 2;
    const auto mapY = [&area] (double value)
    {
        const auto normalized = juce::jlimit (0.0, 1.0, (value + 60.0) / 60.0);
        return (float) area.getBottom() - (float) normalized * (float) area.getHeight();
    };

    g.setFont (monoFont (9.0f));
    g.setColour (COL_MUTED);
    g.drawText ("L", labels.removeFromLeft (columnWidth), juce::Justification::centred);
    labels.removeFromLeft (columnGap);
    g.drawText (meter.channels > 1 ? "R" : "—", labels, juce::Justification::centred);

    for (int channel = 0; channel < 2; ++channel)
    {
        auto column = area.withX (area.getX() + channel * (columnWidth + columnGap))
                          .withWidth (columnWidth);
        g.setColour (COL_MUTED.withAlpha (0.16f));
        for (int db = -48; db <= -6; db += 6)
            g.drawHorizontalLine (juce::roundToInt (mapY ((double) db)),
                                  (float) column.getX(), (float) column.getRight());
        const bool available = currentAvailable && channel < meter.channels
                            && std::isfinite (meter.sample_peak_dbfs[channel]);
        if (available)
        {
            const auto levelY = mapY (meter.sample_peak_dbfs[channel]);
            g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.62f));
            g.fillRect ((float) column.getX(), levelY,
                        (float) column.getWidth(), (float) column.getBottom() - levelY);
            if (std::isfinite (meter.channel_true_peak_dbtp[channel]))
            {
                g.setColour (COL_SPECTRUM_DELTA_BR);
                g.drawHorizontalLine (juce::roundToInt (
                    mapY (meter.channel_true_peak_dbtp[channel])),
                    (float) column.getX(), (float) column.getRight());
            }
        }
        if (cumulativeAvailable && channel < meter.channels
            && std::isfinite (meter.sample_peak_hold_dbfs[channel]))
        {
            g.setColour (COL_FLORA_BR);
            g.fillRect ((float) column.getX(),
                        mapY (meter.sample_peak_hold_dbfs[channel]),
                        (float) column.getWidth(), 1.0f);
        }
    }

    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawHorizontalLine (clips.getY(), (float) clips.getX(), (float) clips.getRight());
    g.setColour (COL_MUTED);
    g.setFont (labelFont (7.0f));
    g.drawText ("CLIP", clips.removeFromTop (10), juce::Justification::centred);
    const auto left = clips.removeFromLeft (columnWidth);
    clips.removeFromLeft (columnGap);
    paintClipCount (g, left, "L", meter.clip_events[0], cumulativeAvailable && meter.channels > 0);
    paintClipCount (g, clips, "R", meter.clip_events[1], cumulativeAvailable && meter.channels > 1);
}

void View::paintClipEventRail (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& meter = observatoryFrame.meter;
    const bool cumulativeAvailable = cumulativeFactsAvailable();
    g.setColour (BG.withAlpha (0.82f));
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 3.0f, 1.0f);
    area.reduce (5, 1);
    auto label = area.removeFromLeft (juce::jmin (62, area.getWidth() / 3));
    g.setColour (COL_MUTED);
    g.setFont (labelFont (7.5f));
    g.drawText ("CLIP EVENTS", label, juce::Justification::centredLeft);
    const auto left = area.removeFromLeft (area.getWidth() / 2);
    paintClipCount (g, left, "L", meter.clip_events[0], cumulativeAvailable && meter.channels > 0);
    paintClipCount (g, area, "R", meter.clip_events[1], cumulativeAvailable && meter.channels > 1);
}

void View::paintMeasuredMycelium (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& meter = observatoryFrame.meter;
    if (! currentFactsAvailable()
        || ! std::isfinite (meter.lufs_m))
        return;
    const auto energy = (float) juce::jlimit (0.0, 1.0, (meter.lufs_m + 48.0) / 48.0);
    const auto direction = std::isfinite (meter.balance_db)
        ? (float) juce::jlimit (-1.0, 1.0, meter.balance_db / 12.0) : 0.0f;
    for (int branch = 0; branch < 5; ++branch)
    {
        const auto y = (float) area.getBottom() - 5.0f
                     - (float) branch * area.getHeight() * 0.17f;
        juce::Path path;
        path.startNewSubPath ((float) area.getX(), y);
        path.cubicTo ((float) area.getX() + area.getWidth() * (0.24f + direction * 0.05f),
                      y - area.getHeight() * energy * 0.12f,
                      (float) area.getX() + area.getWidth() * (0.60f + direction * 0.08f),
                      y + area.getHeight() * energy * 0.09f,
                      (float) area.getRight(), y - area.getHeight() * energy * 0.04f);
        g.setColour (COL_FLORA.withAlpha (0.035f + energy * 0.035f));
        g.strokePath (path, juce::PathStrokeType (0.7f + energy * 0.45f));
    }
}
}
