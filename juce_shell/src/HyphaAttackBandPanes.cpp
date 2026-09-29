#include "HyphaAttackBandPainter.h"

#include <cmath>
#include <initializer_list>

#include "HyphaAttackDepth.h"
#include "HyphaAttackLanePainter.h"
#include "HyphaAttackStage.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

// The HEAD / TAIL panes of the selected hit in the chosen band, drawn from the one record the
// engine gave for that hit: its values and its envelopes can never come from different hits or
// bands. When the panes have nothing to draw, TAIL says why, in one message.
namespace hypha::attack_band_painter
{
namespace
{
using typography::TextRole;
constexpr auto visualization = typography::Composition::visualization;
const auto postColour = juce::Colour (attack_ui::waveformColour);
const auto preColour = juce::Colour (attack_ui::preTraceColour);
const auto onsetColour = juce::Colour (attack_ui::selectionColour);

juce::Rectangle<int> rectangleOf (attack_ui::Box box)
{
    return { box.x, box.y, box.width, box.height };
}

int lineHeight (const presentation::Context& context, TextRole role)
{
    return text_style::requiredLineHeight (typography::resolve (context, role, visualization));
}

juce::String signedMs (float ms, int decimals)
{
    const auto step = std::pow (10.0f, static_cast<float> (decimals));
    auto rounded = std::round (ms * step) / step;
    if (rounded == 0.0f)
        rounded = 0.0f;
    return (rounded >= 0.0f ? "+" : "") + juce::String (rounded, decimals) + " ms";
}

// A pane's parts: the caption line, one plot (or PRE above POST) and the time labels.
struct Parts
{
    juce::Rectangle<int> caption, labels;
    std::array<juce::Rectangle<float>, 2> plots {};
    std::size_t rows = 1;
};

Parts partsOf (juce::Rectangle<int> pane, const presentation::Context& context, bool twoRows)
{
    Parts parts;
    auto inner = pane.reduced (6, 3);
    parts.caption = inner.removeFromTop (lineHeight (context, TextRole::legend));
    parts.labels = inner.removeFromBottom (lineHeight (context, TextRole::axis));
    auto plot = inner.toFloat().reduced (2.0f, 1.0f);
    if (twoRows && plot.getHeight() >= 24.0f)
    {
        parts.rows = 2;
        parts.plots[0] = plot.removeFromTop ((plot.getHeight() - 4.0f) * 0.5f);
        plot.removeFromTop (4.0f);
    }
    parts.plots[parts.rows - 1] = plot;
    return parts;
}

struct Axis
{
    juce::Rectangle<float> plot;
    float fromMs = 0.0f;
    float toMs = 1.0f;

    float x (float ms) const noexcept
    {
        return plot.getX() + plot.getWidth() * (ms - fromMs) / (toMs - fromMs);
    }

    float y (float db) const noexcept
    {
        const auto level = std::isfinite (db)
            ? juce::jlimit (0.0f, 1.0f, (db - attack_ui::absoluteFloorDb) / -attack_ui::absoluteFloorDb)
            : 0.0f;
        return plot.getBottom() - level * plot.getHeight();
    }
};

Axis axisFor (juce::Rectangle<float> plot, bool head) noexcept
{
    return head ? Axis { plot, attack_band::headFromMs, attack_band::headToMs }
                : Axis { plot, attack_band::tailFromMs, attack_band::tailToMs };
}

bool drawable (const KirinAttackBandSide& side) noexcept
{
    return side.state == KIRIN_ATTACK_BAND_SIDE_RISES || side.state == KIRIN_ATTACK_BAND_SIDE_RINGS_ON
        || side.state == KIRIN_ATTACK_BAND_SIDE_SILENT;
}

bool timed (const KirinAttackBandSide& side) noexcept
{
    return side.state == KIRIN_ATTACK_BAND_SIDE_RISES
        && side.arrival_state == KIRIN_ATTACK_BAND_ARRIVAL_AT;
}

bool released (const KirinAttackBandSide& side) noexcept
{
    return side.state == KIRIN_ATTACK_BAND_SIDE_RISES
        && side.release_state == KIRIN_ATTACK_BAND_RELEASE_AT;
}

// The envelope points inside the pane's window: the edge, or the body closed to the floor.
template <std::size_t N>
juce::Path envelopePath (const float (&points)[N], float pointsFromMs, float pointsToMs,
                         const Axis& axis, bool closed)
{
    juce::Path path;
    const auto step = (pointsToMs - pointsFromMs) / static_cast<float> (N);
    bool started = false;
    float lastX = 0.0f;
    for (std::size_t point = 0; point < N; ++point)
    {
        const auto ms = pointsFromMs + (static_cast<float> (point) + 0.5f) * step;
        if (ms < axis.fromMs || ms > axis.toMs)
            continue;
        const juce::Point<float> at { axis.x (ms), axis.y (points[point]) };
        if (! started)
        {
            if (closed)
            {
                path.startNewSubPath (at.x, axis.plot.getBottom());
                path.lineTo (at);
            }
            else
                path.startNewSubPath (at);
            started = true;
        }
        else
            path.lineTo (at);
        lastX = at.x;
    }
    if (started && closed)
    {
        path.lineTo (lastX, axis.plot.getBottom());
        path.closeSubPath();
    }
    return path;
}

juce::Path pathFor (const KirinAttackBandEnvelope& envelope, bool head, const Axis& axis, bool closed)
{
    return head ? envelopePath (envelope.head_dbfs, attack_band::headPointsFromMs,
                                attack_band::headPointsToMs, axis, closed)
                : envelopePath (envelope.tail_dbfs, attack_band::tailFromMs,
                                attack_band::tailToMs, axis, closed);
}

void paintSide (juce::Graphics& g, const KirinAttackBandEnvelope& envelope, bool head,
                const Axis& axis, bool post)
{
    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (axis.plot.getSmallestIntegerContainer());
    const auto edge = pathFor (envelope, head, axis, false);
    if (! post)
    {
        // The PRE reference stays a plain light line over the POST body, as in HISTORY.
        g.setColour (preColour.withAlpha (0.85f));
        g.strokePath (edge, juce::PathStrokeType (0.9f));
        return;
    }
    const auto body = pathFor (envelope, head, axis, true);
    const auto& depth = attack_depth::look();
    const auto& plot = axis.plot;
    if (depth.lift > 0.0f)
    {
        g.setColour (juce::Colours::black.withAlpha (juce::jmin (1.0f, 0.85f * depth.lift)));
        g.fillPath (body, juce::AffineTransform::translation (1.0f, 1.5f));
    }
    g.setGradientFill (juce::ColourGradient (postColour.withAlpha (0.34f), plot.getX(), plot.getY(),
                                             postColour.withAlpha (0.04f), plot.getX(), plot.getBottom(),
                                             false));
    g.fillPath (body);
    attack_depth::lightVolume (g, edge, edge, postColour, depth.fresnel + 0.03f * depth.bloom,
                               depth.specular, 1.4f);
    g.setColour (postColour.withAlpha (0.20f));
    g.strokePath (edge, juce::PathStrokeType (2.6f));
    g.setColour (postColour.withAlpha (0.92f));
    g.strokePath (edge, juce::PathStrokeType (1.0f));
}

// A measured time between two marks: a bracket with its value beside it, ivory like every value.
void paintBracket (juce::Graphics& g, const Axis& axis, float fromMs, float toMs, float y,
                   const juce::String& text, const presentation::Context& context)
{
    const auto x0 = axis.x (fromMs);
    const auto x1 = axis.x (toMs);
    g.setColour (COL_OBSERVATORY_VALUE.withAlpha (0.9f));
    g.drawLine (x0, y, x1, y, 1.2f);
    g.drawLine (x0, y - 4.0f, x0, y + 4.0f, 1.0f);
    g.drawLine (x1, y - 4.0f, x1, y + 4.0f, 1.0f);
    g.setFont (monoFont (context, TextRole::readout, visualization));
    const auto right = juce::jmax (x0, x1) + 5.0f;
    const auto width = 84.0f;
    const bool fitsRight = right + width <= axis.plot.getRight();
    g.setColour (COL_OBSERVATORY_VALUE);
    text_style::drawText (g, text,
                          fitsRight ? juce::Rectangle<float> (right, y - 9.0f, width, 18.0f)
                                    : juce::Rectangle<float> (juce::jmin (x0, x1) - 5.0f - width, y - 9.0f,
                                                              width, 18.0f),
                          fitsRight ? juce::Justification::centredLeft : juce::Justification::centredRight,
                          false);
}

void paintMarks (juce::Graphics& g, const KirinAttackBandHit& hit, bool head, const Axis& axis,
                 bool pre, bool post, const presentation::Context& context)
{
    const auto& plot = axis.plot;
    const auto releaseEnd = [] (const KirinAttackBandSide& side) { return side.peak_ms + side.release_ms; };
    const auto mark = [&] (const KirinAttackBandSide& side, juce::Colour colour) {
        g.setColour (colour.withAlpha (head ? 0.70f : 0.85f));
        if (head && timed (side))
            g.drawVerticalLine (juce::roundToInt (axis.x (side.arrival_ms)), plot.getY(), plot.getBottom());
        if (! head && released (side))
        {
            const auto x = axis.x (releaseEnd (side));
            const auto y = axis.y (side.level_dbfs - 20.0f);
            g.drawLine (x, y - 6.0f, x, y + 6.0f, 1.2f);
        }
    };
    if (pre) mark (hit.pre, preColour);
    if (post) mark (hit.post, postColour);
    if (! (pre && post))
        return;
    if (head && timed (hit.pre) && timed (hit.post))
        paintBracket (g, axis, hit.pre.arrival_ms, hit.post.arrival_ms, plot.getY() + 9.0f,
                      signedMs (hit.post.arrival_ms - hit.pre.arrival_ms, 1), context);
    if (! head && released (hit.pre) && released (hit.post))
    {
        const auto y = juce::jmin (axis.y (hit.pre.level_dbfs - 20.0f),
                                   axis.y (hit.post.level_dbfs - 20.0f)) - 12.0f;
        paintBracket (g, axis, releaseEnd (hit.pre), releaseEnd (hit.post),
                      juce::jmax (plot.getY() + 9.0f, y),
                      signedMs (hit.post.release_ms - hit.pre.release_ms, 0), context);
    }
}

// The one message a pane shows instead of envelopes, or nothing when it draws them.
juce::String messageFor (const PaneFrame& frame)
{
    if (frame.needsPlay)
        return playText (frame.band);
    if (! frame.selected)
        return "NO HIT";
    if (frame.hit == nullptr)
        return "MEASURING";
    const auto& hit = frame.hit->hit;
    // The sides the panes show: both while POST - PRE is shown, POST alone otherwise.
    const auto shown = [&frame, &hit] (auto&& test) {
        return (frame.delta && test (hit.pre)) || test (hit.post); };
    if (shown ([] (const KirinAttackBandSide& side) { return side.state == KIRIN_ATTACK_BAND_SIDE_NOT_KEPT; }))
        return playText (frame.band);
    if (shown ([] (const KirinAttackBandSide& side) { return side.state == KIRIN_ATTACK_BAND_SIDE_PENDING; }))
        return "MEASURING";
    if (! shown ([] (const KirinAttackBandSide& side) { return drawable (side); }))
        return "NO PAIR";
    if (! shown ([] (const KirinAttackBandSide& side) {
            return drawable (side) && side.state != KIRIN_ATTACK_BAND_SIDE_SILENT; }))
        return "NO SOUND";
    return {};
}

void paintPaneValues (juce::Graphics& g, juce::Rectangle<int> pane, bool head,
                      const KirinAttackBandHitEnvelope& record, const presentation::Context& context,
                      bool twoRows, bool delta)
{
    // A start hidden by the previous hit's ring-out has no arrival mark here; the DELAY and ATT
    // readouts beside the panes say RINGING, once.
    const auto& hit = record.hit;
    const auto parts = partsOf (pane, context, twoRows);
    for (std::size_t row = 0; row < parts.rows; ++row)
    {
        const auto axis = axisFor (parts.plots[row], head);
        const bool pre = delta && (parts.rows == 1 || row == 0) && drawable (hit.pre);
        const bool post = (parts.rows == 1 || row == 1) && drawable (hit.post);
        if (post) paintSide (g, record.post, head, axis, true);
        if (pre) paintSide (g, record.pre, head, axis, false);
        paintMarks (g, hit, head, axis, pre, post, context);
        if (parts.rows == 2)
        {
            g.setColour (COL_TEXT_TERTIARY);
            g.setFont (monoFont (context, TextRole::legend, visualization)
                           .withExtraKerningFactor (attack_stage::captionTracking (context)));
            text_style::drawText (g, row == 0 ? "PRE" : "POST",
                                  parts.plots[row].getSmallestIntegerContainer().reduced (4, 1),
                                  juce::Justification::topLeft, false);
        }
    }
}
}

void paintPaneChrome (juce::Graphics& g, const attack_ui::Layout& layout,
                      const presentation::Context& context, bool twoRows, bool delta,
                      attack_band::PreBand pre)
{
    const auto& depth = attack_depth::look();
    for (const bool head : { true, false })
    {
        const auto pane = rectangleOf (head ? attack_band::headPane (layout) : attack_band::tailPane (layout));
        if (pane.isEmpty())
            continue;
        attack_stage::paint (g, pane.toFloat(), 4.0f, 0.30f, true);
        const auto parts = partsOf (pane, context, twoRows);
        g.setFont (monoFont (context, TextRole::legend, visualization)
                       .withExtraKerningFactor (attack_stage::captionTracking (context)));
        g.setColour (COL_TEXT_SECONDARY);
        text_style::drawText (g, head ? "HEAD" : "TAIL", parts.caption, juce::Justification::centredLeft, false);
        g.setColour (COL_TEXT_TERTIARY);
        const juce::String sides = delta ? "PRE / POST"
                                 : pre == attack_band::PreBand::predates ? "POST / UPDATE PRE" : "POST";
        attack_lane_painter::drawFitting (g, { head ? sides : juce::String ("PEAK -20 dB") },
                                          parts.caption, context, TextRole::legend,
                                          juce::Justification::centredRight);
        g.setFont (monoFont (context, TextRole::axis, visualization));
        text_style::drawText (g, head ? "-5" : "0", parts.labels, juce::Justification::centredLeft, false);
        text_style::drawText (g, head ? "+40 ms" : "+300 ms", parts.labels, juce::Justification::centredRight, false);
        for (std::size_t row = 0; row < parts.rows; ++row)
        {
            const auto axis = axisFor (parts.plots[row], head);
            const auto& plot = axis.plot;
            g.setColour (COL_TEXT_TERTIARY.withAlpha (0.14f));
            for (const auto db : { -24.0f, -48.0f })
                g.drawHorizontalLine (juce::roundToInt (axis.y (db)), plot.getX(), plot.getRight());
            g.setColour (postColour.withAlpha (0.14f));
            g.drawHorizontalLine (juce::roundToInt (plot.getBottom() - 0.5f), plot.getX(), plot.getRight());
            attack_depth::engraveLip (g, plot.getBottom() - 0.5f, plot.getX(), plot.getRight());
            if (depth.graticule > 0.0f)
            {
                g.setColour (COL_TEXT_TERTIARY.withAlpha (juce::jmin (1.0f, depth.graticule * 2.0f)));
                for (int db = -12; db > static_cast<int> (attack_ui::absoluteFloorDb); db -= 12)
                {
                    const auto y = juce::roundToInt (axis.y (static_cast<float> (db)));
                    g.drawHorizontalLine (y, plot.getX(), plot.getX() + 3.0f);
                    g.drawHorizontalLine (y, plot.getRight() - 3.0f, plot.getRight());
                }
                // Time ticks on the walls: HEAD every 5 ms, TAIL every 50 ms, longer at the tens.
                const auto tick = head ? 5.0f : 50.0f;
                for (auto ms = axis.fromMs; ms <= axis.toMs; ms += tick)
                {
                    const auto x = juce::roundToInt (axis.x (ms));
                    const auto length = std::fmod (ms, tick * 2.0f) == 0.0f ? 4.0f : 2.0f;
                    g.drawVerticalLine (x, plot.getY(), plot.getY() + length);
                    g.drawVerticalLine (x, plot.getBottom() - length, plot.getBottom());
                }
            }
            if (head)
            {
                // The onset is the zero of HEAD's time axis.
                g.setColour (onsetColour.withAlpha (0.45f));
                g.drawVerticalLine (juce::roundToInt (axis.x (0.0f)), plot.getY(), plot.getBottom());
            }
        }
    }
}

void paintPanes (juce::Graphics& g, const attack_ui::Layout& layout,
                 const presentation::Context& context, const PaneFrame& frame)
{
    if (const auto message = messageFor (frame); message.isNotEmpty())
    {
        // Said once, in the wider TAIL pane, so it reads the same at every size; HEAD keeps its
        // empty axes. Where TAIL is too narrow the band's name is dropped; the label cell names it.
        const auto pane = rectangleOf (attack_band::tailPane (layout));
        if (pane.isEmpty())
            return;
        const auto parts = partsOf (pane, context, false);
        const auto shorter = message == playText (frame.band) ? playText (0) : message;
        g.setColour (COL_TEXT_SECONDARY);
        attack_lane_painter::drawFitting (g, { message, shorter },
                                          parts.plots[0].getSmallestIntegerContainer().reduced (4, 0),
                                          context, TextRole::status, juce::Justification::centred);
        return;
    }
    for (const bool head : { true, false })
    {
        const auto pane = rectangleOf (head ? attack_band::headPane (layout) : attack_band::tailPane (layout));
        if (! pane.isEmpty())
            paintPaneValues (g, pane, head, *frame.hit, context, frame.twoRows, frame.delta);
    }
}
}
