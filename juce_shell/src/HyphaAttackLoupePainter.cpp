#include "HyphaAttackLoupePainter.h"

#include <algorithm>
#include <cmath>

#include "HyphaAttackStage.h"
#include "HyphaAttackUiContract.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

namespace hypha::attack_loupe
{
namespace
{
constexpr auto visualization = typography::Composition::visualization;
const auto preColour = juce::Colour (0xffa3b3b9); // the HISTORY PRE trace colour

bool usable (const KirinAttackDetail* detail) noexcept
{
    return detail != nullptr && detail->sample_rate > 0
        && detail->shape_count >= 2 && detail->shape_count <= KIRIN_ATTACK_SHAPE_CAPACITY
        && detail->shape_start_sample < detail->event_sample
        && detail->event_sample < detail->shape_end_sample;
}

struct Axis
{
    juce::Rectangle<float> plot;
    std::int64_t first = 0;
    std::int64_t last = 1;

    float x (std::int64_t sample) const noexcept
    {
        const auto fraction = static_cast<long double> (sample - first)
                            / static_cast<long double> (last - first);
        return plot.getX() + plot.getWidth() * static_cast<float> (fraction);
    }

    float y (float db) const noexcept
    {
        const auto level = std::isfinite (db)
            ? juce::jlimit (0.0f, 1.0f, (db - attack_ui::absoluteFloorDb) / -attack_ui::absoluteFloorDb)
            : 0.0f;
        return plot.getBottom() - level * plot.getHeight();
    }
};

float peakDb (float linear) noexcept
{
    return std::isfinite (linear) && linear > 0.0f ? 20.0f * std::log10 (linear)
                                                   : attack_ui::absoluteFloorDb;
}

juce::Path shapePath (const KirinAttackDetail& detail, const Axis& axis, bool closed)
{
    juce::Path path;
    const auto span = static_cast<long double> (detail.shape_end_sample - detail.shape_start_sample);
    for (std::uint32_t point = 0; point < detail.shape_count; ++point)
    {
        const auto sample = detail.shape_start_sample + static_cast<std::int64_t> (
            span * (static_cast<long double> (point) + 0.5L) / detail.shape_count);
        const juce::Point<float> at { axis.x (sample), axis.y (peakDb (detail.shape[point])) };
        if (point == 0)
        {
            if (closed)
            {
                path.startNewSubPath (axis.x (detail.shape_start_sample), axis.plot.getBottom());
                path.lineTo (at);
            }
            else
                path.startNewSubPath (at);
        }
        else
            path.lineTo (at);
    }
    if (closed)
    {
        path.lineTo (axis.x (detail.shape_end_sample), axis.plot.getBottom());
        path.closeSubPath();
    }
    return path;
}

// Context RMS, the step at the onset, then attack RMS: the operands of TRANSIENT and STRENGTH.
void paintRmsSteps (juce::Graphics& g, const KirinAttackDetail& detail, const Axis& axis,
                    juce::Colour colour, float thickness)
{
    juce::Path steps;
    const auto onset = axis.x (detail.event_sample);
    steps.startNewSubPath (axis.x (detail.shape_start_sample), axis.y (detail.context_rms_dbfs));
    steps.lineTo (onset, axis.y (detail.context_rms_dbfs));
    steps.lineTo (onset, axis.y (detail.attack_rms_dbfs));
    steps.lineTo (axis.x (detail.shape_end_sample), axis.y (detail.attack_rms_dbfs));
    g.setColour (colour);
    g.strokePath (steps, juce::PathStrokeType (thickness));
}

juce::String relativeMs (std::int64_t sample, const KirinAttackDetail& anchor)
{
    const auto ms = std::round (static_cast<double> (sample - anchor.event_sample) * 1'000.0
                                / static_cast<double> (anchor.sample_rate));
    return (ms > 0.0 ? "+" : "") + juce::String (static_cast<int> (ms)) + " ms";
}
}

void paintPanel (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.getWidth() >= 60 && area.getHeight() >= 60)
        attack_stage::paint (g, area.toFloat(), 4.0f, 0.10f);
}

void paint (juce::Graphics& g, juce::Rectangle<int> area, const KirinAttackDetail* pre,
            const KirinAttackDetail* post, presentation::Context context)
{
    if (area.getWidth() < 60 || area.getHeight() < 60)
        return;
    auto inner = area.reduced (6, 3);
    const auto legendStyle = typography::resolve (context, typography::TextRole::legend, visualization);
    const auto axisStyle = typography::resolve (context, typography::TextRole::axis, visualization);
    auto title = inner.removeFromTop (text_style::requiredLineHeight (legendStyle));
    auto axisRow = inner.removeFromBottom (text_style::requiredLineHeight (axisStyle));
    g.setFont (attack_stage::trackedFont (context, typography::TextRole::legend,
                                          attack_stage::captionTracking (context)));
    g.setColour (COL_TEXT_SECONDARY);
    g.drawText ("HIT 130 ms", title, juce::Justification::centredLeft, false);
    const auto preUsable = usable (pre);
    const auto postUsable = usable (post);
    if (! preUsable && ! postUsable)
    {
        g.setColour (COL_TEXT_TERTIARY);
        g.drawText ("NO HIT", inner, juce::Justification::centred, false);
        return;
    }
    g.setColour (COL_TEXT_TERTIARY);
    g.drawText (preUsable && postUsable ? "PRE / POST" : postUsable ? "POST" : "PRE",
                title, juce::Justification::centredRight, false);

    const auto& anchor = postUsable ? *post : *pre;
    Axis axis { inner.toFloat().reduced (2.0f, 2.0f), anchor.shape_start_sample,
                anchor.shape_end_sample };
    if (preUsable && postUsable)
    {
        axis.first = std::min (pre->shape_start_sample, post->shape_start_sample);
        axis.last = std::max (pre->shape_end_sample, post->shape_end_sample);
    }
    const auto& plot = axis.plot;
    for (const auto db : { -24.0f, -48.0f })
    {
        g.setColour (COL_TEXT_TERTIARY.withAlpha (0.16f));
        g.drawHorizontalLine (juce::roundToInt (axis.y (db)), plot.getX(), plot.getRight());
    }
    const auto attackLeft = axis.x (anchor.event_sample);
    const auto attackRight = axis.x (anchor.shape_end_sample);
    g.setColour (juce::Colour (attack_ui::transientColour).withAlpha (0.08f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (
        attackLeft, plot.getY(), attackRight, plot.getBottom()));
    g.setColour (juce::Colour (attack_ui::selectionColour).withAlpha (0.55f));
    g.drawVerticalLine (juce::roundToInt (attackLeft), plot.getY(), plot.getBottom());
    if (preUsable && postUsable && pre->event_sample != post->event_sample)
    {
        g.setColour (preColour.withAlpha (0.55f));
        g.drawVerticalLine (juce::roundToInt (axis.x (pre->event_sample)), plot.getY(),
                            plot.getBottom());
    }

    if (postUsable)
    {
        const auto waveform = juce::Colour (attack_ui::waveformColour);
        const auto edge = shapePath (*post, axis, false);
        juce::ColourGradient body (waveform.withAlpha (0.34f), plot.getX(), plot.getY(),
                                   waveform.withAlpha (0.04f), plot.getX(), plot.getBottom(), false);
        g.setGradientFill (body);
        g.fillPath (shapePath (*post, axis, true));
        g.setColour (waveform.withAlpha (0.20f));
        g.strokePath (edge, juce::PathStrokeType (2.6f));
        g.setColour (waveform.withAlpha (0.92f));
        g.strokePath (edge, juce::PathStrokeType (1.0f));
    }
    if (preUsable)
    {
        g.setColour (preColour.withAlpha (0.80f));
        g.strokePath (shapePath (*pre, axis, false), juce::PathStrokeType (0.9f));
        paintRmsSteps (g, *pre, axis, preColour.withAlpha (0.70f), 0.9f);
    }
    if (postUsable)
    {
        paintRmsSteps (g, *post, axis, juce::Colour (attack_ui::transientColour), 1.4f);
        if (std::isfinite (post->sample_peak_dbfs) && std::isfinite (post->attack_rms_dbfs))
        {
            const auto x = axis.x (post->shape_end_sample) - 3.0f;
            const auto top = axis.y (post->sample_peak_dbfs);
            const auto bottom = axis.y (post->attack_rms_dbfs);
            g.setColour (juce::Colour (attack_ui::crestColour));
            g.drawLine (x, top, x, bottom, 1.2f);
            g.drawLine (x - 2.5f, top, x + 2.5f, top, 1.0f);
        }
    }

    // The onset line is the zero; only the window ends are labelled so the short attack window
    // never collides with its own label.
    g.setFont (monoFont (context, typography::TextRole::axis, visualization));
    g.setColour (COL_TEXT_TERTIARY);
    g.drawText (relativeMs (axis.first, anchor), axisRow, juce::Justification::centredLeft, false);
    g.drawText (relativeMs (axis.last, anchor), axisRow, juce::Justification::centredRight, false);
}
}
