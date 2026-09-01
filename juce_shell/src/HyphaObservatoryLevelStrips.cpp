#include "HyphaObservatoryView.h"

#include <array>
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

void paintFullChannelStrips (juce::Graphics& g,
                             juce::Rectangle<int> area,
                             const KirinMeterSession& meter,
                             bool currentAvailable,
                             bool cumulativeAvailable)
{
    auto labels = area.removeFromTop (18);
    auto readouts = area.removeFromTop (46);
    auto clips = area.removeFromBottom (23);
    constexpr int scaleWidth = 22;
    constexpr int columnGap = 4;
    const int columnWidth = (area.getWidth() - scaleWidth - 2 * columnGap) / 2;
    const auto leftColumn = area.withWidth (columnWidth);
    const auto scaleColumn = area.withX (leftColumn.getRight() + columnGap)
                                 .withWidth (scaleWidth);
    const auto rightColumn = area.withX (scaleColumn.getRight() + columnGap)
                                 .withWidth (columnWidth);
    const std::array<juce::Rectangle<int>, 2> columns { leftColumn, rightColumn };

    g.setColour (COL_MUTED);
    g.setFont (monoFont (11.0f));
    g.drawText ("L", labels.withX (leftColumn.getX()).withWidth (columnWidth),
                juce::Justification::centred);
    g.drawText (meter.channels > 1 ? "R" : "—",
                labels.withX (rightColumn.getX()).withWidth (columnWidth),
                juce::Justification::centred);

    for (int channel = 0; channel < 2; ++channel)
    {
        auto readout = readouts.withX (columns[(size_t) channel].getX())
                               .withWidth (columnWidth);
        const bool available = currentAvailable && channel < meter.channels
                            && std::isfinite (meter.channel_true_peak_dbtp[channel]);
        g.setColour (COL_MUTED);
        g.setFont (labelFont (8.0f));
        g.drawText ("TP", readout.removeFromTop (12), juce::Justification::centred);
        g.setColour (available ? COL_NORMAL : COL_MUTED);
        drawTabularText (g, monoFont (14.0f),
                         available ? juce::String (meter.channel_true_peak_dbtp[channel], 1)
                                   : juce::String ("---"),
                         readout.removeFromTop (21).toFloat(),
                         juce::Justification::centred);
        g.setColour (COL_MUTED);
        g.setFont (labelFont (7.0f));
        g.drawText ("dBTP", readout, juce::Justification::centred);
    }

    const auto mapY = [&area] (double value)
    {
        const auto normalized = juce::jlimit (0.0, 1.0, (value + 48.0) / 48.0);
        return (float) area.getBottom() - (float) normalized * (float) area.getHeight();
    };
    g.setFont (monoFont (7.0f));
    for (int db = 0; db >= -48; db -= 6)
    {
        const auto y = juce::roundToInt (mapY ((double) db));
        g.setColour (COL_MUTED.withAlpha (0.22f));
        for (const auto column : columns)
            g.drawHorizontalLine (y, (float) column.getX(), (float) column.getRight());
        g.setColour (COL_MUTED.withAlpha (0.82f));
        g.drawText (juce::String (db), scaleColumn.withY (y - 5).withHeight (10),
                    juce::Justification::centred);
    }

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto column = columns[(size_t) channel];
        const bool available = currentAvailable && channel < meter.channels
                            && std::isfinite (meter.sample_peak_dbfs[channel]);
        if (available)
        {
            for (int db = -48; db < 0; db += 2)
            {
                if ((double) db > meter.sample_peak_dbfs[channel])
                    continue;
                const auto top = mapY ((double) db + 1.45);
                const auto bottom = mapY ((double) db);
                g.setColour ((db >= -6 ? COL_FLORA : COL_SPECTRUM_DELTA)
                                 .withAlpha (db >= -6 ? 0.72f : 0.68f));
                g.fillRect ((float) column.getX(), top,
                            (float) column.getWidth(), juce::jmax (1.0f, bottom - top));
            }
            if (std::isfinite (meter.channel_true_peak_dbtp[channel]))
            {
                g.setColour (COL_FLORA_BR.withAlpha (0.94f));
                g.drawHorizontalLine (juce::roundToInt (
                    mapY (meter.channel_true_peak_dbtp[channel])),
                    (float) column.getX(), (float) column.getRight());
            }
        }
        if (cumulativeAvailable && channel < meter.channels
            && std::isfinite (meter.sample_peak_hold_dbfs[channel]))
        {
            g.setColour (COL_NORMAL.withAlpha (0.62f));
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
    paintClipCount (g, clips.withX (leftColumn.getX()).withWidth (columnWidth),
                    "L", meter.clip_events[0], cumulativeAvailable && meter.channels > 0);
    paintClipCount (g, clips.withX (rightColumn.getX()).withWidth (columnWidth),
                    "R", meter.clip_events[1], cumulativeAvailable && meter.channels > 1);
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

void View::setDisplayedEditorSize (int width, int height)
{
    if (! validEditorSize (width, height))
        return;
    displayedEditorWidth = width;
    displayedSizeLabel = juce::String (juce::roundToInt (
        static_cast<double> (width) * 100.0 / 300.0)) + "%";
    sizeButton.setButtonText (displayedSizeLabel);
}

void View::cycleSize()
{
    const auto width = displayedEditorWidth > 0 ? displayedEditorWidth : currentPreset().width;
    size_t current = sizePresets.size() - 1u;
    for (size_t index = 0; index < sizePresets.size(); ++index)
        if (sizePresets[index].width > width)
        {
            current = index == 0u ? sizePresets.size() - 1u : index - 1u;
            break;
        }
        else if (sizePresets[index].width == width)
        {
            current = index;
            break;
        }
    if (onSizeChange)
        onSizeChange (sizePresets[(current + 1u) % sizePresets.size()]);
}

void View::paintChannelStrips (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& meter = observatoryFrame.meter;
    const bool currentAvailable = currentFactsAvailable();
    const bool cumulativeAvailable = cumulativeFactsAvailable();
    g.setColour (BG.withAlpha (experienceFamily() == ExperienceFamily::compactMeter
                                   ? 0.96f : 0.76f));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);
    area.reduce (5, 5);
    if (currentPreset().density == Density::observatory)
    {
        paintFullChannelStrips (g, area, meter, currentAvailable, cumulativeAvailable);
        return;
    }
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

}
