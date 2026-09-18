#include "HyphaMonoSumPainter.h"

#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <array>
#include <cmath>

namespace hypha::mono_sum_curve
{
namespace
{
    constexpr float kTopDb = 0.0f;
    constexpr float kMidDb = KIRIN_MONO_SUM_DISPLAY_MIDPOINT_DB;
    constexpr float kFloorDb = KIRIN_MONO_SUM_DISPLAY_FLOOR_DB;

    struct AxisTick
    {
        float hz;
        const char* label;
    };
    constexpr std::array<AxisTick, 4> kAxisTicks {{
        { 10.0f, "10" }, { 100.0f, "100" }, { 1'000.0f, "1k" }, { 10'000.0f, "10k" },
    }};

    /// Band centres share FREQ's definition: band i spans min*(max/min)^(i/N) to the next edge.
    float bandEdgeHz (size_t index) noexcept
    {
        const float ratio = KIRIN_MONO_SUM_MAX_HZ / KIRIN_MONO_SUM_MIN_HZ;
        return KIRIN_MONO_SUM_MIN_HZ
             * std::pow (ratio, (float) index / (float) KIRIN_MONO_SUM_BAND_COUNT);
    }

    float xForBand (size_t index, juce::Rectangle<float> plot) noexcept
    {
        const auto position = ((float) index + 0.5f) / (float) KIRIN_MONO_SUM_BAND_COUNT;
        return juce::jmap (position, plot.getX(), plot.getRight());
    }

    float xForHz (float hz, juce::Rectangle<float> plot) noexcept
    {
        const float ratio = KIRIN_MONO_SUM_MAX_HZ / KIRIN_MONO_SUM_MIN_HZ;
        const auto position = std::log (hz / KIRIN_MONO_SUM_MIN_HZ) / std::log (ratio);
        return juce::jmap (juce::jlimit (0.0f, 1.0f, position), plot.getX(), plot.getRight());
    }

    void drawScale (juce::Graphics& g, juce::Rectangle<float> plot, bool compact,
                    presentation::Context presentation)
    {
        g.setColour (COL_MUTED.withAlpha (0.42f));
        for (const auto db : { kTopDb, kMidDb, kFloorDb })
        {
            const auto y = yForDb (db, plot);
            g.drawLine (plot.getX(), y, plot.getRight(), y, db == kMidDb ? 0.6f : 0.8f);
        }
        // The smallest size shows MONO on its own, so it is the size that most needs the scale.
        // Labels are drawn at every size; only the gutter they sit in is narrower.
        g.setFont (monoFont (presentation, typography::TextRole::axis,
                             typography::Composition::visualization));
        g.setColour (COL_TEXT_TERTIARY);
        const float gutter = compact ? 18.0f : 23.0f;
        for (const auto db : { kTopDb, kMidDb, kFloorDb })
        {
            const auto y = yForDb (db, plot);
            const auto label = db == 0.0f ? juce::String ("0")
                                          : juce::String (juce::roundToInt (db));
            // The floor label is held inside the plot so it does not collide with the frequency
            // row directly under it.
            const auto top = juce::jlimit (plot.getY() - 7.0f, plot.getBottom() - 14.0f, y - 7.0f);
            g.drawText (label,
                        juce::Rectangle<float> { plot.getX() - gutter - 3.0f, top, gutter, 14.0f }
                            .toNearestInt(),
                        juce::Justification::centredRight);
        }
        for (const auto& tick : kAxisTicks)
        {
            // The 10 Hz tick sits on the plot's left edge, so its label is kept inside the plot
            // instead of hanging back into the value gutter.
            const auto left = juce::jlimit (plot.getX(), plot.getRight() - 28.0f,
                                            xForHz (tick.hz, plot) - 14.0f);
            g.drawText (tick.label,
                        juce::Rectangle<float> { left, plot.getBottom() + 1.0f, 28.0f, 12.0f }
                            .toNearestInt(),
                        juce::Justification::centred);
        }
    }

    /// The boundary under which one observation holds fewer than three cycles. Marked, not hidden:
    /// the values stay on the curve and a divider says where they stop being exact.
    void drawApproximateBoundary (juce::Graphics& g, juce::Rectangle<float> plot,
                                  float approximateBelowHz, bool compact,
                                  presentation::Context presentation)
    {
        if (! (approximateBelowHz > KIRIN_MONO_SUM_MIN_HZ)
            || approximateBelowHz >= KIRIN_MONO_SUM_MAX_HZ)
            return;

        const auto x = xForHz (approximateBelowHz, plot);
        g.setColour (COL_MUTED.withAlpha (0.38f));
        g.drawLine (x, plot.getY(), x, plot.getBottom(), 0.7f);
        if (x - plot.getX() < 10.0f)
            return;

        g.setFont (monoFont (presentation, typography::TextRole::axis,
                             typography::Composition::visualization));
        g.setColour (COL_TEXT_TERTIARY);
        g.drawText ("~", juce::Rectangle<float> { plot.getX(), plot.getY() + 1.0f,
                                                  x - plot.getX() - 2.0f, 12.0f }.toNearestInt(),
                    juce::Justification::centredRight);
    }

    /// Runs of bands that were measured. A band with nothing to measure breaks the line rather
    /// than being drawn at 0 dB, which is the one reading that means the band loses nothing.
    void drawCurve (juce::Graphics& g, juce::Rectangle<float> plot,
                    const KirinMeterSession& meter, float strokeWidth)
    {
        juce::Path run;
        bool open = false;
        int pointsInRun = 0;
        const auto flush = [&] {
            if (! open)
                return;
            if (pointsInRun == 1)
            {
                juce::Point<float> only;
                run.getCurrentPosition();
                only = run.getCurrentPosition();
                g.fillEllipse (only.x - strokeWidth, only.y - strokeWidth,
                               strokeWidth * 2.0f, strokeWidth * 2.0f);
            }
            else
            {
                g.strokePath (run, juce::PathStrokeType (strokeWidth,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
            }
            run.clear();
            open = false;
            pointsInRun = 0;
        };

        g.setColour (COL_SPECTRUM_POST);
        for (size_t band = 0u; band < KIRIN_MONO_SUM_BAND_COUNT; ++band)
        {
            const auto value = meter.mono_sum_db[band];
            if (! std::isfinite (value))
            {
                flush();
                continue;
            }
            const juce::Point<float> point { xForBand (band, plot), yForDb (value, plot) };
            if (! open)
            {
                run.startNewSubPath (point);
                open = true;
                pointsInRun = 1;
            }
            else
            {
                run.lineTo (point);
                ++pointsInRun;
            }
        }
        flush();
    }
}

float yForDb (float db, juce::Rectangle<float> plot) noexcept
{
    const auto middle = plot.getY() + plot.getHeight() * 0.5f;
    if (! std::isfinite (db))
        return plot.getBottom();
    if (db >= kMidDb)
        return juce::jmap (juce::jlimit (kMidDb, kTopDb, db), kTopDb, kMidDb,
                           plot.getY(), middle);
    return juce::jmap (juce::jlimit (kFloorDb, kMidDb, db), kMidDb, kFloorDb,
                       middle, plot.getBottom());
}

bool bandIsApproximate (size_t band, float approximateBelowHz) noexcept
{
    return std::isfinite (approximateBelowHz) && bandEdgeHz (band) < approximateBelowHz;
}

bool hasBands (const KirinMeterSession& meter, bool available) noexcept
{
    return available && meter.channels == 2
        && meter.mono_sum_band_count == KIRIN_MONO_SUM_BAND_COUNT;
}

juce::String stateText (const KirinMeterSession& meter, bool available)
{
    if (! available)
        return hypha::emDash();
    if (meter.channels != 2)
        return "MONO INPUT";
    return hasBands (meter, available) ? juce::String ("dB") : hypha::emDash();
}

void paint (juce::Graphics& g,
            juce::Rectangle<int> area,
            const KirinMeterSession& meter,
            bool available,
            bool compact,
            bool showTitle,
            presentation::Context presentation)
{
    const bool bands = hasBands (meter, available);
    const auto state = stateText (meter, available);
    if (showTitle)
    {
        auto title = area.removeFromTop (compact ? 12 : 15);
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (monoFont (presentation, typography::TextRole::legend,
                             typography::Composition::visualization));
        g.drawText (compact ? "MONO" : "MONO SUM", title, juce::Justification::centredLeft);
        g.setColour (bands ? COL_SPECTRUM_POST : COL_MUTED);
        g.drawText (state, title, juce::Justification::centredRight);
    }

    auto plot = area.reduced (0, compact ? 3 : 4).toFloat();
    plot = plot.withTrimmedLeft (compact ? 22.0f : 27.0f)
               .withTrimmedRight (compact ? 3.0f : 4.0f)
               .withTrimmedBottom (compact ? 12.0f : 13.0f);
    if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    drawScale (g, plot, compact, presentation);
    if (! bands)
    {
        if (! compact)
        {
            g.setColour (COL_TEXT_SECONDARY);
            g.setFont (monoFont (presentation, typography::TextRole::status,
                                 typography::Composition::visualization));
            g.drawText (state, plot.toNearestInt(), juce::Justification::centred);
        }
        return;
    }

    drawApproximateBoundary (g, plot, meter.mono_sum_approximate_below_hz, compact, presentation);
    drawCurve (g, plot, meter, compact ? 1.2f : 1.6f);
}
}
