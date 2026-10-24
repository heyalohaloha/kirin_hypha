#include "HyphaAttackV2Painter.h"
#include "HyphaAttackBandContract.h"
#include "HyphaAttackLanePainter.h"
#include "HyphaAttackLoupePainter.h"
#include "HyphaAttackPainter.h"
#include "HyphaAttackStage.h"
#include "HyphaMainFrame.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"
#include "HyphaLanguage.h"
#include <algorithm>
#include <cmath>

namespace hypha::attack_v2
{
namespace
{
constexpr auto composition = typography::Composition::visualization;
const auto cyan = juce::Colour (attack_ui::selectionColour);
const std::array<const char*, 4> bandLabels { "DELAY", "ATT", "REL", "LEVEL" };
const std::array<const char*, 4> allLabels { "TRANSIENT", "STRENGTH", "CREST", "SHARPNESS" };
void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> area,
           const presentation::Context& c, typography::TextRole role, juce::Colour colour,
           juce::Justification alignment = juce::Justification::centredLeft)
{
    if (area.isEmpty()) return;
    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (area);
    // A frame squeezed below the role's glyphs (the smallest DRUM sizes) takes the smallest
    // semantic role instead of an ad-hoc height; the clip keeps the rest inside its frame.
    const bool squeezed = monoFont (c, role, composition).getHeight() > static_cast<float> (area.getHeight());
    g.setFont (monoFont (c, squeezed ? typography::TextRole::axis : role, composition));
    g.setColour (colour); text_style::drawText (g, s, area, alignment, false);
}
bool navigationHistory (const State& state)
{ return state.band == 0 || state.selection.live; }
std::int64_t historyEnd (const State& state)
{
    if (navigationHistory (state)) return state.clock.position();
    const auto end = state.cohort ? state.cohort->header.cutoff_sample
                                 : state.selection.selected ? state.selection.selected->event_sample : state.clock.position();
    return state.selection.selected ? std::max (end, state.selection.selected->event_sample) : end;
}
std::uint32_t historyRate (const State& state)
{
    return !navigationHistory (state) && state.cohort ? state.cohort->header.source.sample_rate
        : state.navigationHeader().source.sample_rate;
}
void representativeLoupe (juce::Graphics& g, const RepresentativeLoupe& loupe,
                          juce::Rectangle<int> area, const presentation::Context& context)
{
    const auto dpi = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto scale = std::isfinite (dpi) && dpi > 0 && dpi <= 4 ? dpi : 1.0f;
    const auto width = static_cast<int> (std::ceil (area.getWidth() * scale));
    const auto height = static_cast<int> (std::ceil (area.getHeight() * scale));
    if (! loupe.image.isValid() || loupe.image.getWidth() != width || loupe.image.getHeight() != height
        || loupe.context != context || loupe.scale != scale || loupe.language != i18n::revision())
    {
        loupe.image = juce::Image (juce::Image::ARGB, width, height, true);
        juce::Graphics pixels (loupe.image); pixels.addTransform (juce::AffineTransform::scale (scale));
        const auto& s = *loupe.packet;
        attack_loupe::paint (pixels, area.withZeroOrigin(), s.has_representative_pre ? &s.representative_pre : nullptr,
                             s.has_representative_post ? &s.representative_post : nullptr, context);
        loupe.context = context; loupe.scale = scale; loupe.language = i18n::revision();
    }
    g.drawImageTransformed (loupe.image, juce::AffineTransform::scale (1.0f / scale)
        .translated (static_cast<float> (area.getX()), static_cast<float> (area.getY())));
}
void timeStrip (juce::Graphics& g, const State& state, const Geometry& shape, const presentation::Context& c)
{
    const bool all = state.band == 0;
    const auto* cohort = state.cohort.get();
    const auto end = historyEnd (state);
    const auto rate = historyRate (state);
    attack_stage::paint (g, shape.history.toFloat(), 3, .18f, all || shape.head.isEmpty());
    if (all || shape.head.isEmpty())
    {
        const auto first = end - static_cast<std::int64_t> (rate) * 6;
        attack_painter::drawEnvelope (g, state.waveform, shape.history, first, end, rate,
                                     attack_painter::WaveformStyle::continuous, 0.8f);
        if (state.target == KIRIN_TARGET_DELTA)
            attack_painter::drawEnvelope (g, state.preWaveform, shape.history, first, end, rate,
                                         attack_painter::WaveformStyle::trace, 0.7f);
    }
    juce::Graphics::ScopedSaveState saved (g); g.reduceClipRegion (shape.history);
    const auto labelHeight = presentation::densityIndex (c.density) >= 3 ? 14 : 11;
    for (const auto& located : visibleEvents (state, shape))
    {
        const bool selected = state.selection.selected && sameEvent (located.key, *state.selection.selected)
            && state.needsSingle();
        g.setColour (selected ? cyan : COL_NORMAL.withAlpha (.55f));
        g.drawVerticalLine (juce::roundToInt (located.x), static_cast<float> (shape.history.getY()),
                           static_cast<float> (shape.history.getBottom() - (all ? 0 : labelHeight)));
    }
    auto axis = shape.history.withTrimmedTop (std::max (0, shape.history.getHeight() - labelHeight)).reduced (3, 0);
    if (all) text (g, words ("Full-band 6s history", u8"全帯域6秒履歴"), axis, c, typography::TextRole::axis, COL_NORMAL);
    else
    {
        auto label = cohort ? words ("Cohort ", u8"対象") + juce::String (cohort->cohort_count) + " /6s"
                            : words ("Cohort waiting", u8"対象打音待ち");
        if (shape.head.isEmpty()) label = words ("Full-band ", u8"全帯域 ") + label;
        if (! shape.head.isEmpty())
        {
            label += state.target == KIRIN_TARGET_DELTA ? " PRE/POST" : " POST";
            if (cohort) label += " " + words ("Set change: gap", u8"組が変わる所で線を切る");
        }
        text (g, label, axis, c, typography::TextRole::axis, COL_NORMAL,
              juce::Justification::centred);
        if (rate != 0)
        {
            text (g, juce::String (static_cast<double> (end) / rate - 6.0, 2) + "s", axis, c, typography::TextRole::axis, COL_NORMAL);
            text (g, juce::String (static_cast<double> (end) / rate, 2) + "s", axis, c, typography::TextRole::axis, COL_NORMAL,
                  juce::Justification::centredRight);
        }
    }
}
juce::String caption (const State& state)
{
    const auto& p = state.adopted;
    const juce::String band = attack_band::labelFor (state.band);
    if (p.previousReadout)
    {
        const auto& previous = *p.previousReadout;
        return "Previous / " + band + (previous.target == KIRIN_TARGET_DELTA ? juce::String::fromUTF8 (u8" Δ ") : " POST ")
            + juce::String (previous.count) + words (" hits", u8"打");
    }
    const auto target = state.target == KIRIN_TARGET_DELTA ? juce::String::fromUTF8 (u8"Δ") : juce::String ("POST");
    if (p.single || state.needsSingle())
    {
        const auto event = state.selection.selected;
        const bool past = event && (event->event_sample < p.viewport - static_cast<std::int64_t> (event->source.sample_rate) * 6);
        const bool ahead = event && event->event_sample > p.viewport;
        return band + " " + target + " " + (event ? juce::String (static_cast<double> (event->event_sample)
             / event->source.sample_rate, 2) + "s" + (past ? words (" Past", u8" 過去") : ahead ? words (" Ahead", " Ahead") : "")
             : words ("Waiting", u8"待機"));
    }
    const auto count = p.summary ? p.summary->cohort_count : 0;
    const auto span = p.summary && count > 1 ? static_cast<double> (p.summary->events[count - 1].event_sample
        - p.summary->events[0].event_sample) / p.header.source.sample_rate : 0.0;
    auto label = band + " " + target + " " + juce::String (count) + words (" hits ", u8"打 ") + juce::String (span, 1) + "s";
    return label;
}
}
std::vector<LocatedEvent> visibleEvents (const State& state, const Geometry& shape)
{
    std::vector<LocatedEvent> result;
    const auto rate = historyRate (state);
    const auto end = historyEnd (state);
    result.reserve (navigationHistory (state) ? state.events.size() : 9);
    const auto add = [&] (const KirinSnapshotEventKey& key) {
        if (key.event_sample > end || end - key.event_sample > static_cast<std::int64_t> (rate) * 6) return;
        result.push_back ({key, eventX (key.event_sample, end, rate, shape.history)});
    };
    if (navigationHistory (state)) for (const auto& key : state.events) add (key);
    else
    {
        if (state.cohort) for (std::size_t i = 0; i < state.cohort->cohort_count; ++i) add (state.cohort->events[i]);
        if (state.selection.selected && std::none_of (result.begin(), result.end(), [&] (const auto& e) {
            return sameEvent (e.key, *state.selection.selected); })) add (*state.selection.selected);
    }
    return result;
}

void paintV2 (juce::Graphics& g, const State& state, const Geometry& shape, const presentation::Context& c)
{
    const auto& p = state.adopted;
    const auto readoutColour = p.previousReadout ? COL_TEXT_SECONDARY : COL_NORMAL;
    for (std::size_t i = 0; i < shape.bands.size(); ++i)
    {
        if (shape.bands[i].isEmpty()) continue;
        surface_material::paintControl (g, shape.bands[i].reduced (1).toFloat(), false, false,
                                       state.band == i, cyan, 2);
        text (g, attack_band::labelFor (static_cast<std::uint8_t> (i)), shape.bands[i], c,
              typography::TextRole::action, state.band == i ? cyan : COL_NORMAL, juce::Justification::centred);
    }
    text (g, state.overlay ? "VIEW OVERLAY" : "VIEW 2 ROWS", shape.view, c, typography::TextRole::action,
          COL_NORMAL, juce::Justification::centredRight);
    auto label = caption (state);
    if (state.selection.clustered)
    {
        label = words ("Choice ", u8"候補") + juce::String (state.selection.index + 1) + "/"
              + juce::String (state.selection.frozen.size()) + " " + juce::String (attack_band::labelFor (state.band));
        label += state.target == KIRIN_TARGET_DELTA ? juce::String::fromUTF8 (u8" Δ") : juce::String (" POST");
        if (state.selection.selected) label += " " + juce::String (static_cast<double> (state.selection.selected->event_sample)
             / state.selection.selected->source.sample_rate, 2) + "s";
        text (g, "<", shape.previous, c, typography::TextRole::action, cyan, juce::Justification::centred);
        text (g, ">", shape.next, c, typography::TextRole::action, cyan, juce::Justification::centred);
    }
    text (g, label, state.selection.clustered ? shape.cluster : shape.caption, c, typography::TextRole::readout, readoutColour);
    text (g, state.selection.live ? (p.clock == ClockState::hold ? "HOLD" : "LIVE") : "LOCK",
          shape.live, c, typography::TextRole::action, cyan, juce::Justification::centred);
    text (g, words ("Facts", u8"根拠"), shape.evidence, c, typography::TextRole::action, COL_NORMAL, juce::Justification::centredRight);
    const bool panes = state.band != 0 && ! shape.head.isEmpty();
    if (panes) paintEnvelopes (g, p, shape, c, state.overlay);
    timeStrip (g, state, shape, c);
    if (! shape.loupe.isEmpty())
    {
        attack_loupe::paintPanel (g, shape.loupe);
        if (! p.single && state.band == 0 && p.representative)
            representativeLoupe (g, *p.representative, shape.loupe, c);
        else
            attack_loupe::paint (g, shape.loupe,
                p.single && p.single->has_all_pre ? &p.single->all_pre : nullptr,
                p.single && p.single->has_all_post ? &p.single->all_post : nullptr, c);
    }
    for (std::size_t i = 0; i < 4; ++i)
    {
        const auto& lane = p.lanes[i]; const auto& area = shape.lanes[i];
        const auto colour = juce::Colour (std::array<std::uint32_t, 4> { attack_ui::transientColour,
            attack_ui::strengthColour, attack_ui::crestColour, attack_ui::sharpnessColour }[i]);
        text (g, state.band == 0 ? allLabels[i] : bandLabels[i], area.metric, c, typography::TextRole::metricLabel, colour);
        const auto& number = p.previousReadout ? p.previousReadout->numbers[i] : lane.heldNumber ? *lane.heldNumber : lane.number;
        auto unit = number.unit;
        if (!p.previousReadout && ! lane.heldNumber && lane.scope == Scope::confirmedSubset && lane.number.exponent != 0)
            unit = state.band != 0 ? (i == 3 ? state.target == KIRIN_TARGET_DELTA ? "dB" : "dBFS" : "ms")
                : i == 3 ? "acum" : i == 1 && state.target != KIRIN_TARGET_DELTA ? "dBFS" : "dB";
        text (g, unit, area.unit, c, typography::TextRole::unit, readoutColour, shape.cards ? juce::Justification::centredRight : juce::Justification::centredLeft);
        // Production reads whole-cohort values or the chosen single. Partial statistics and
        // classifications stay unchanged in the adopted presentation and existing Facts.
        const auto mainValue = p.previousReadout ? (number.valid ? number.value : juce::String::fromUTF8 (u8"—"))
            : lane.heldNumber ? lane.heldNumber->value
            : lane.scope == Scope::confirmedSubset || lane.scope == Scope::noScalar || ! lane.number.valid
                ? juce::String::fromUTF8 (u8"—") : lane.number.value;
        text (g, mainValue, area.value, c, typography::TextRole::primaryValue, readoutColour,
              shape.cards ? juce::Justification::centredLeft : juce::Justification::centredRight);
        g.setColour (COL_TEXT_SECONDARY.withAlpha (.15f));
        g.drawHorizontalLine (area.value.getBottom() - 1, static_cast<float> (area.metric.getX()), static_cast<float> (area.value.getRight()));
        if (state.band != 0 && ! area.axis.isEmpty() && state.cohort && state.cohort->header.target == state.target)
        {
            const auto scale = attack_lanes::scaleFor (attack_lanes::bandLanes[i], state.target == KIRIN_TARGET_DELTA);
            const auto dotX = [&] (double value) { return area.axis.getX() + (area.axis.getWidth() - 1)
                * static_cast<float> (std::clamp ((value - scale.minimum) / (scale.maximum - scale.minimum), 0.0, 1.0)); };
            const auto plotY = area.axis.getY() + 8;
            g.setColour (COL_NORMAL.withAlpha (.25f)); g.drawHorizontalLine (plotY,
                static_cast<float> (area.axis.getX()), static_cast<float> (area.axis.getRight()));
            text (g, juce::String (scale.minimum, 0), area.axis.withTrimmedTop (area.axis.getHeight() - 16), c,
                  typography::TextRole::axis, COL_NORMAL.withAlpha (.6f));
            text (g, juce::String (scale.maximum, 0), area.axis.withTrimmedTop (area.axis.getHeight() - 16), c,
                  typography::TextRole::axis, COL_NORMAL.withAlpha (.6f), juce::Justification::centredRight);
            for (std::size_t hit = 0; hit < state.cohort->cohort_count; ++hit)
            {
                const auto& e = state.cohort->evidence[hit][i];
                if (! e.has_interval) continue;
                const auto endpoint = e.interval.lower.kind == KIRIN_ENDPOINT_FINITE ? e.interval.lower : e.interval.upper;
                if (endpoint.kind != KIRIN_ENDPOINT_FINITE) continue;
                const bool selected = ! state.selection.live && state.selection.selected
                    && sameEvent (*state.selection.selected, state.cohort->events[hit]);
                g.setColour (selected ? cyan : colour);
                const auto x = dotX (endpoint.value), y = static_cast<float> (plotY);
                if (e.interval.lower.kind == KIRIN_ENDPOINT_FINITE && e.interval.upper.kind == KIRIN_ENDPOINT_FINITE
                    && e.interval.lower.value < e.interval.upper.value)
                {
                    const auto right = dotX (e.interval.upper.value);
                    g.drawLine (x, y, right, y, 1);
                    g.drawLine (x, y - 3, x, y + 3, 1); g.drawLine (right, y - 3, right, y + 3, 1);
                }
                else if (e.interval.lower.kind == KIRIN_ENDPOINT_NEGATIVE_INFINITY
                    || e.interval.upper.kind == KIRIN_ENDPOINT_POSITIVE_INFINITY
                    || endpoint.value < scale.minimum || endpoint.value > scale.maximum)
                {
                    const auto direction = e.interval.lower.kind == KIRIN_ENDPOINT_NEGATIVE_INFINITY
                        || endpoint.value < scale.minimum ? -1.0f : 1.0f;
                    g.drawLine (x - direction * 6, y - 3, x, y, 1);
                    g.drawLine (x - direction * 6, y + 3, x, y, 1);
                }
                else g.fillEllipse (x - 2, y - 2, 4, 4);
            }
        }
    }
}
}
