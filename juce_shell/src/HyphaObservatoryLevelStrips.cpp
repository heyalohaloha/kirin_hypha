#include "HyphaObservatoryView.h"

#include <cmath>

namespace hypha::observatory
{
void View::paintChannelStrips (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (BG.withAlpha (0.82f));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);
    area.reduce (5, 5);
    auto labels = area.removeFromTop (15);
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
        const bool available = meterAvailable && channel < meter.channels
                            && std::isfinite (meter.sample_peak_dbfs[channel]);
        if (! available)
            continue;
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
        if (std::isfinite (meter.sample_peak_hold_dbfs[channel]))
        {
            g.setColour (COL_FLORA_BR);
            g.fillRect ((float) column.getX(),
                        mapY (meter.sample_peak_hold_dbfs[channel]),
                        (float) column.getWidth(), 1.0f);
        }
    }
}

void View::paintMeasuredMycelium (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (! meterAvailable || meter.state != KIRIN_METER_SESSION_ACTIVE
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
