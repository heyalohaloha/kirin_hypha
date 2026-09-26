#include "HyphaMonoSumPainter.h"
#include "HyphaStoppedHistory.h"

#include "HyphaTheme.h"
#include "HyphaTextStyle.h"
#include "HyphaTimeFieldImage.h"

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

    void drawFrequencyLabels (juce::Graphics& g, juce::Rectangle<float> plot, float rowTop,
                              presentation::Context presentation)
    {
        g.setFont (monoFont (presentation, typography::TextRole::axis,
                             typography::Composition::visualization));
        g.setColour (COL_TEXT_TERTIARY);
        for (const auto& tick : kAxisTicks)
        {
            // The 10 Hz tick sits on the plot's left edge, so its label is kept inside the plot
            // instead of hanging back into the value gutter.
            const auto left = juce::jlimit (plot.getX(), plot.getRight() - 28.0f,
                                            xForHz (tick.hz, plot) - 14.0f);
            g.drawText (tick.label,
                        juce::Rectangle<float> { left, rowTop, 28.0f, 12.0f }.toNearestInt(),
                        juce::Justification::centred);
        }
    }

    void drawScale (juce::Graphics& g, juce::Rectangle<float> plot, bool compact,
                    presentation::Context presentation)
    {
        // The three lines carry their own label and weight rather than being compared back to the
        // constants they came from: the midpoint's line is the lighter one because it divides the
        // scale, not because its value happens to equal kMidDb.
        struct ScaleLine
        {
            float db;
            const char* label;
            float thickness;
        };
        const std::array<ScaleLine, 3> lines {{
            { kTopDb, "0", 0.8f },
            { kMidDb, "-6", 0.6f },
            { kFloorDb, "-24", 0.8f },
        }};

        g.setColour (COL_MUTED.withAlpha (0.42f));
        for (const auto& line : lines)
        {
            const auto y = yForDb (line.db, plot);
            g.drawLine (plot.getX(), y, plot.getRight(), y, line.thickness);
        }
        // The smallest size shows MONO on its own, so it is the size that most needs the scale.
        // Labels are drawn at every size; only the gutter they sit in is narrower.
        g.setFont (monoFont (presentation, typography::TextRole::axis,
                             typography::Composition::visualization));
        g.setColour (COL_TEXT_TERTIARY);
        const float gutter = compact ? 18.0f : 26.0f;
        for (const auto& line : lines)
        {
            const auto y = yForDb (line.db, plot);
            // The floor label is held inside the plot so it does not collide with the frequency
            // row directly under it.
            const auto top = juce::jlimit (plot.getY() - 7.0f, plot.getBottom() - 14.0f, y - 7.0f);
            g.drawText (line.label,
                        juce::Rectangle<float> { plot.getX() - gutter - 3.0f, top, gutter, 14.0f }
                            .toNearestInt(),
                        juce::Justification::centredRight);
        }
    }

    /// The boundary under which one observation holds fewer than three cycles. Marked, not hidden:
    /// the values stay on the curve and a divider says where they stop being exact.
    void drawApproximateBoundary (juce::Graphics& g, juce::Rectangle<float> plot,
                                  float approximateBelowHz,
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

    /// The six seconds behind the curve: newest along the bottom edge, rising as it ages. The row
    /// rule is the one FREQ's field uses, so a host's own publishing cadence cannot stripe it and
    /// a real break in the measurement stays a break.
    void drawTimeField (juce::Graphics& g, juce::Rectangle<float> plot,
                        const mono_sum_history::History& history,
                        presentation::Context presentation)
    {
        if (history.empty() || plot.getHeight() < 6.0f)
            return;

        const auto field = time_field::build (
            { (int) mono_sum_history::capacity, (int) KIRIN_MONO_SUM_BAND_COUNT,
              mono_sum_history::seconds },
            history.size(),
            [&history] (size_t index) { return history.ageSeconds (index); },
            [&history] (size_t index, int column) {
                return history.at (index).db[(size_t) column];
            },
            COL_SPECTRUM_POST,
            [] (float db) { return fieldAlphaStepFor (db); });
        time_field::draw (g, field, plot);

        g.setColour (COL_MUTED.withAlpha (0.30f));
        g.drawRect (plot, 0.5f);
        g.setFont (monoFont (presentation, typography::TextRole::axis,
                             typography::Composition::visualization));
        g.setColour (COL_TEXT_TERTIARY);
        g.drawText ("6s", juce::Rectangle<float> { plot.getX() - 29.0f, plot.getY(),
                                                   26.0f, 11.0f }.toNearestInt(),
                    juce::Justification::centredRight);
        // "0s" rather than "NOW": it pairs with the "6s" above it, and it fits the gutter the
        // dB labels already set, where "NOW" clipped.
        g.drawText ("0s", juce::Rectangle<float> { plot.getX() - 29.0f,
                                                   plot.getBottom() - 11.0f, 26.0f, 11.0f }
                        .toNearestInt(),
                    juce::Justification::centredRight);
    }

    /// Runs of bands with a value. A band without one breaks the line rather than being drawn at
    /// 0 dB, which is the one reading that means the band loses nothing.
    void drawRuns (juce::Graphics& g, juce::Rectangle<float> plot,
                   const std::array<float, KIRIN_MONO_SUM_BAND_COUNT>& values, float strokeWidth)
    {
        juce::Path run;
        bool open = false;
        int pointsInRun = 0;
        const auto flush = [&] {
            if (! open)
                return;
            if (pointsInRun == 1)
            {
                // One band between two without a value still has to be visible, and a path of a
                // single point strokes nothing.
                const auto only = run.getCurrentPosition();
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

        for (size_t band = 0u; band < KIRIN_MONO_SUM_BAND_COUNT; ++band)
        {
            const auto value = values[band];
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

    /// The live curve. A band with nothing to measure in this 100 ms observation (the rest between
    /// two drum hits, say) keeps the value it was last measured at for up to one second, drawn
    /// faintly, so the curve does not blink between hits. Unmeasured for longer, it breaks the
    /// line: nothing is invented, and the stored observations are never changed.
    void drawCurve (juce::Graphics& g, juce::Rectangle<float> plot, const KirinMeterSession& meter,
                    const mono_sum_history::History& history, float strokeWidth)
    {
        std::array<float, KIRIN_MONO_SUM_BAND_COUNT> measured {};
        std::array<float, KIRIN_MONO_SUM_BAND_COUNT> held {};
        bool anyHeld = false;
        for (size_t band = 0u; band < KIRIN_MONO_SUM_BAND_COUNT; ++band)
        {
            measured[band] = held[band] = meter.mono_sum_db[band];
            if (std::isfinite (measured[band]))
                continue;
            for (size_t index = history.size(); index-- > 0u;)
            {
                const auto& entry = history.at (index);
                if (entry.sampleRate == 0u || entry.observedFrames > meter.observed_frames)
                    continue;
                if ((double) (meter.observed_frames - entry.observedFrames) / (double) entry.sampleRate
                    > mono_sum_curve::holdSeconds)
                    break;
                if (std::isfinite (entry.db[band]))
                {
                    held[band] = entry.db[band];
                    anyHeld = true;
                    break;
                }
            }
        }
        if (anyHeld)
        {
            g.setColour (COL_SPECTRUM_POST.withAlpha (mono_sum_curve::heldAlpha));
            drawRuns (g, plot, held, strokeWidth);
        }
        g.setColour (COL_SPECTRUM_POST);
        drawRuns (g, plot, measured, strokeWidth);
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

uint8_t fieldAlphaStepFor (float db) noexcept
{
    // No ink at all means "not measured", so a band that was measured never gets none. At 0 dB the
    // loss is zero and the ink would be too, which would draw a band that loses nothing exactly
    // like a band there was nothing to measure in. The floor keeps those two apart: dark is no
    // observation, the faintest wash is an observation that lost nothing.
    constexpr int measuredMinimum = 3;
    if (! std::isfinite (db))
        return 0u;
    const juce::Rectangle<float> unit { 0.0f, 0.0f, 1.0f, 1.0f };
    const auto position = juce::jlimit (0.0f, 1.0f, yForDb (db, unit));
    return (uint8_t) juce::jlimit (measuredMinimum, 255,
                                   (int) std::lround (position * 0.62f * 255.0f));
}

void paint (juce::Graphics& g,
            juce::Rectangle<int> area,
            const KirinMeterSession& meter,
            const mono_sum_history::History& history,
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
    plot = plot.withTrimmedLeft (compact ? 22.0f : 30.0f)
               .withTrimmedRight (compact ? 3.0f : 4.0f)
               .withTrimmedBottom (compact ? 12.0f : 13.0f);
    if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    // The live curve keeps the upper part and the six seconds behind it take the lower part, so
    // the two never share one vertical axis: the curve's height is dB and the field's is time.
    // They share the frequency axis under both, because they are the same 32 bands.
    const bool withField = ! compact && plot.getHeight() >= 54.0f;
    auto curvePlot = plot;
    juce::Rectangle<float> fieldPlot;
    if (withField)
    {
        fieldPlot = plot.removeFromBottom (plot.getHeight() * 0.42f);
        curvePlot = plot.withTrimmedBottom (3.0f);
    }

    drawScale (g, curvePlot, compact, presentation);
    if (compact)
        drawFrequencyLabels (g, curvePlot, curvePlot.getBottom() + 1.0f, presentation);
    if (! bands)
    {
        // Stopped: the six seconds already measured stay, dimmed; only the live curve goes.
        if (withField && ! available && ! history.empty())
        {
            stopped_history::paintDimmed (g, [&] { drawTimeField (g, fieldPlot, history, presentation); });
            drawFrequencyLabels (g, curvePlot, fieldPlot.getBottom() + 1.0f, presentation);
        }
        if (! compact)
        {
            g.setColour (COL_TEXT_SECONDARY);
            g.setFont (monoFont (presentation, typography::TextRole::status,
                                 typography::Composition::visualization));
            g.drawText (state, curvePlot.toNearestInt(), juce::Justification::centred);
        }
        return;
    }

    if (withField)
        drawTimeField (g, fieldPlot, history, presentation);
    if (! compact)
        drawFrequencyLabels (g, curvePlot, (withField ? fieldPlot : curvePlot).getBottom() + 1.0f,
                             presentation);
    drawApproximateBoundary (g, curvePlot, meter.mono_sum_approximate_below_hz, presentation);
    drawCurve (g, curvePlot, meter, history, compact ? 1.2f : 1.6f);
}
}
