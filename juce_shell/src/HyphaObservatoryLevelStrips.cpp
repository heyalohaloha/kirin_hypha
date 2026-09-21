#include "HyphaObservatoryView.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaChannelReadoutLayout.h"
#include "ChannelRoles.h"

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
                     bool available,
                     presentation::Context presentation)
{
    g.setColour (! available ? COL_MUTED
                             : count > 0 ? COL_FLORA_BR : COL_TEXT_TERTIARY);
    g.setFont (monoFont (presentation, typography::TextRole::readout,
                         typography::Composition::instrument));
    const auto value = available ? clipCountText (count) : hypha::emDash();
    drawTabularText (g, monoFont (presentation, typography::TextRole::readout,
                                  typography::Composition::instrument),
                     juce::String (channel) + " " + value,
                     area.toFloat(), juce::Justification::centred);
}

void paintFullChannelStrips (juce::Graphics& g,
                             juce::Rectangle<int> area,
                             const KirinMeterSession& meter,
                             bool currentAvailable,
                             bool cumulativeAvailable,
                             presentation::Context presentation)
{
    const auto valueFont = monoFont (presentation, typography::TextRole::secondaryValue,
                                    typography::Composition::instrument);
    auto title = area.removeFromTop (16);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentation, typography::TextRole::metricLabel,
                         typography::Composition::instrument));
    g.drawText ("TP", title, juce::Justification::centredLeft);
    g.setFont (labelFont (presentation, typography::TextRole::unit,
                         typography::Composition::instrument));
    g.drawText ("dBTP", title, juce::Justification::centredRight);
    const auto readoutHeight = (int) std::ceil (valueFont.getHeight()) + 2;
    for (int channel = 0; channel < 2; ++channel)
    {
        auto row = area.removeFromTop (readoutHeight);
        const bool available = currentAvailable && channel < meter.channels
                            && std::isfinite (meter.channel_true_peak_dbtp[channel]);
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (monoFont (presentation, typography::TextRole::legend,
                            typography::Composition::instrument));
        g.drawText (channel == 0 ? "L" : "R", row.withWidth (16), juce::Justification::centredLeft);
        g.setColour (available ? COL_NORMAL : COL_MUTED);
        drawTabularText (g, valueFont,
                         available ? juce::String (meter.channel_true_peak_dbtp[channel], 1)
                                   : juce::String ("---"),
                         channelPeakValueArea (row).toFloat(), juce::Justification::centredRight);
    }
    auto labels = area.removeFromTop (18);
    auto clips = area.removeFromBottom (28);
    constexpr int scaleWidth = 22;
    constexpr int columnGap = 4;
    const int columnWidth = (area.getWidth() - scaleWidth - 2 * columnGap) / 2;
    const auto leftColumn = area.withWidth (columnWidth);
    const auto scaleColumn = area.withX (leftColumn.getRight() + columnGap)
                                 .withWidth (scaleWidth);
    const auto rightColumn = area.withX (scaleColumn.getRight() + columnGap)
                                 .withWidth (columnWidth);
    const std::array<juce::Rectangle<int>, 2> columns { leftColumn, rightColumn };

    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (monoFont (presentation, typography::TextRole::legend,
                         typography::Composition::instrument));
    g.drawText ("L", labels.withX (leftColumn.getX()).withWidth (columnWidth),
                juce::Justification::centred);
    g.drawText (meter.channels > 1 ? juce::String ("R") : hypha::emDash(),
                labels.withX (rightColumn.getX()).withWidth (columnWidth),
                juce::Justification::centred);

    const auto mapY = [&area] (double value)
    {
        const auto normalized = juce::jlimit (0.0, 1.0, (value + 48.0) / 48.0);
        return (float) area.getBottom() - (float) normalized * (float) area.getHeight();
    };
    g.setFont (monoFont (presentation, typography::TextRole::axis,
                         typography::Composition::instrument));
    for (int db = 0; db >= -48; db -= 6)
    {
        const auto y = juce::roundToInt (mapY ((double) db));
        const auto labelY = juce::jlimit (area.getY(), area.getBottom() - 14, y - 7);
        g.setColour (COL_MUTED.withAlpha (0.22f));
        for (const auto column : columns)
            g.drawHorizontalLine (y, (float) column.getX(), (float) column.getRight());
        g.setColour (COL_TEXT_TERTIARY);
        g.drawText (juce::String (db),
                    juce::Rectangle<int> { scaleColumn.getX(), labelY,
                                           scaleColumn.getWidth(), 14 },
                    juce::Justification::centred);
    }

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto column = columns[(size_t) channel];
        const bool available = currentAvailable && channel < meter.channels
                            && std::isfinite (meter.sample_peak_dbfs[channel]);
        if (available)
        {
            // One block per dB keeps the Observatory precise at 200/300% while preserving a
            // discrete instrument scale. The former 2 dB blocks became visibly coarse when the
            // logical 600x400 plate was enlarged by the host.
            for (int db = -48; db < 0; ++db)
            {
                if ((double) db > meter.sample_peak_dbfs[channel])
                    continue;
                const auto top = mapY ((double) db + 0.72);
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
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentation, typography::TextRole::metricLabel,
                          typography::Composition::instrument));
    g.drawText ("CLIP", clips.removeFromTop (14), juce::Justification::centred);
    paintClipCount (g, clips.withX (leftColumn.getX()).withWidth (columnWidth),
                    "L", meter.clip_events[0], cumulativeAvailable && meter.channels > 0,
                    presentation);
    paintClipCount (g, clips.withX (rightColumn.getX()).withWidth (columnWidth),
                    "R", meter.clip_events[1], cumulativeAvailable && meter.channels > 1,
                    presentation);
}

void paintSurroundChannelRows (juce::Graphics& g,
                               juce::Rectangle<int> area,
                               const KirinMeterSession& meter,
                               bool currentAvailable,
                               bool cumulativeAvailable,
                               presentation::Context presentation)
{
    auto title = area.removeFromTop (18);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentation, typography::TextRole::metricLabel,
                          typography::Composition::instrument));
    g.drawText ("5.1 CHANNEL PEAK", title, juce::Justification::centredLeft);
    g.setFont (labelFont (presentation, typography::TextRole::unit,
                          typography::Composition::instrument));
    g.drawText ("dBTP / CLIP", title, juce::Justification::centredRight);

    const auto channels = juce::jlimit (0, static_cast<int> (KIRIN_MAX_CHANNELS),
                                        static_cast<int> (meter.channels));
    const auto rows = juce::jmax (1, channels);
    for (int channel = 0; channel < rows; ++channel)
    {
        auto row = channel + 1 == rows
            ? area : area.removeFromTop (area.getHeight() / (rows - channel));
        if (row.getHeight() <= 0)
            continue;

        const auto role = channel < channels
            ? kirin::channelRoleShortName (meter.channel_positions[channel]) : "?";
        auto label = row.removeFromLeft (juce::jmin (34, row.getWidth()));
        auto clip = row.removeFromRight (juce::jmin (30, row.getWidth()));
        auto value = row.removeFromRight (juce::jmin (44, row.getWidth()));
        auto bar = row.reduced (3, juce::jmax (2, row.getHeight() / 4));

        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (monoFont (presentation, typography::TextRole::legend,
                             typography::Composition::instrument));
        g.drawText (role, label, juce::Justification::centredLeft);

        const bool peakAvailable = currentAvailable && channel < channels
                                && std::isfinite (meter.sample_peak_dbfs[channel]);
        const auto mapX = [&bar] (double db)
        {
            const auto normalized = juce::jlimit (0.0, 1.0, (db + 60.0) / 60.0);
            return (float) bar.getX() + static_cast<float> (normalized)
                                      * static_cast<float> (bar.getWidth());
        };
        g.setColour (COL_MUTED.withAlpha (0.22f));
        g.fillRoundedRectangle (bar.toFloat(), 1.5f);
        if (peakAvailable)
        {
            const auto right = mapX (meter.sample_peak_dbfs[channel]);
            g.setColour ((meter.sample_peak_dbfs[channel] >= -6.0
                              ? COL_FLORA : COL_SPECTRUM_DELTA).withAlpha (0.76f));
            g.fillRoundedRectangle (bar.withRight (juce::roundToInt (right)).toFloat(), 1.5f);
            if (std::isfinite (meter.channel_true_peak_dbtp[channel]))
            {
                g.setColour (COL_FLORA_BR);
                const auto x = mapX (meter.channel_true_peak_dbtp[channel]);
                g.drawVerticalLine (juce::roundToInt (x), (float) bar.getY(),
                                    (float) bar.getBottom());
            }
        }
        if (cumulativeAvailable && channel < channels
            && std::isfinite (meter.sample_peak_hold_dbfs[channel]))
        {
            g.setColour (COL_NORMAL.withAlpha (0.72f));
            const auto x = mapX (meter.sample_peak_hold_dbfs[channel]);
            g.drawVerticalLine (juce::roundToInt (x), (float) bar.getY(),
                                (float) bar.getBottom());
        }

        const bool truePeakAvailable = currentAvailable && channel < channels
                                    && std::isfinite (meter.channel_true_peak_dbtp[channel]);
        g.setColour (truePeakAvailable ? COL_NORMAL : COL_MUTED);
        drawTabularText (
            g, monoFont (presentation, typography::TextRole::readout,
                         typography::Composition::instrument),
            truePeakAvailable ? juce::String (meter.channel_true_peak_dbtp[channel], 1)
                              : juce::String ("---"),
            value.toFloat(), juce::Justification::centredRight);
        const bool clipAvailable = cumulativeAvailable && channel < channels;
        g.setColour (! clipAvailable ? COL_MUTED
                                     : meter.clip_events[channel] > 0
                                         ? COL_FLORA_BR : COL_TEXT_TERTIARY);
        drawTabularText (
            g, monoFont (presentation, typography::TextRole::legend,
                         typography::Composition::instrument),
            clipAvailable ? clipCountText (meter.clip_events[channel]) : hypha::emDash(),
            clip.toFloat(), juce::Justification::centredRight);
    }
}
}

SizePreset View::currentPreset() const noexcept
{
    for (const auto preset : sizePresets)
        if (preset.width == getWidth() && preset.height == getHeight())
            return preset;
    return { getWidth(), getHeight(), densityForWidth (getWidth()), "SIZE" };
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
    surface_material::paintPanel (
        g, area.toFloat(), experienceFamily() == ExperienceFamily::compactMeter
                               ? 0.96f : 0.76f);
    area.reduce (5, 5);
    if (meter.channels > 2)
    {
        paintSurroundChannelRows (g, area, meter, currentAvailable, cumulativeAvailable,
                                  presentationContext());
        return;
    }
    if (isFullDensity (currentPreset().density))
    {
        paintFullChannelStrips (g, area, meter, currentAvailable, cumulativeAvailable,
                                presentationContext());
        return;
    }
    auto labels = area.removeFromTop (15);
    auto clips = area.removeFromBottom (28);
    const auto columnGap = 4;
    const auto columnWidth = (area.getWidth() - columnGap) / 2;
    const auto mapY = [&area] (double value)
    {
        const auto normalized = juce::jlimit (0.0, 1.0, (value + 60.0) / 60.0);
        return (float) area.getBottom() - (float) normalized * (float) area.getHeight();
    };

    g.setFont (monoFont (presentationContext(), typography::TextRole::legend,
                         typography::Composition::instrument));
    g.setColour (COL_TEXT_TERTIARY);
    g.drawText ("L", labels.removeFromLeft (columnWidth), juce::Justification::centred);
    labels.removeFromLeft (columnGap);
    g.drawText (meter.channels > 1 ? juce::String ("R") : hypha::emDash(), labels,
                juce::Justification::centred);

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
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentationContext(), typography::TextRole::metricLabel,
                          typography::Composition::instrument));
    g.drawText ("CLIP", clips.removeFromTop (14), juce::Justification::centred);
    const auto left = clips.removeFromLeft (columnWidth);
    clips.removeFromLeft (columnGap);
    paintClipCount (g, left, "L", meter.clip_events[0],
                    cumulativeAvailable && meter.channels > 0, presentationContext());
    paintClipCount (g, clips, "R", meter.clip_events[1],
                    cumulativeAvailable && meter.channels > 1, presentationContext());
}

}
