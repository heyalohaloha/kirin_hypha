#include "HyphaAttackComponent.h"
#include "HyphaAttackV2Painter.h"
#include "HyphaLanguage.h"

namespace hypha
{
bool AttackComponent::setNavigationV2 (const KirinSnapshotHeader& h,
    const std::vector<KirinSnapshotEventKey>& keys, const KirinAttackWaveformBatch& post,
    const KirinAttackWaveformBatch& pre, double now, bool realtime)
{
    const auto before = v2->navigationHeader();
    if (! v2->navigate (h, keys, post, pre, now, realtime)) return false;
    if (before.source.generation != 0 && (! attack_v2::sameSource (before.source, h.source)
        || before.authority_revision != h.authority_revision || before.target != h.target
        || h.cutoff_sample < before.cutoff_sample)) evidenceV2.reset();
    resizeV2(); repaint(); return true;
}
bool AttackComponent::setSummarySnapshotV2 (const KirinAttackBandSummaryV2& p, double now)
{ const bool accepted = v2->summary (p, now); if (accepted) repaint(); return accepted; }
bool AttackComponent::setSingleSnapshotV2 (const KirinAttackSingleSnapshotV2& p, double now)
{ const bool accepted = v2->single (p, now); if (accepted) repaint(); return accepted; }
void AttackComponent::retireV2()
{ v2->retire(); evidenceV2.reset(); evidenceScrollV2 = 0; selectionLaneV2 = -1; selectionPlotV2 = {}; repaint(); }
void AttackComponent::beginSnapshotV2()
{
    v2->begin(); evidenceV2.reset(); evidenceScrollV2 = 0; selectionLaneV2 = -1; selectionPlotV2 = {};
    repaint();
}
const attack_v2::Presentation& AttackComponent::presentationSnapshotV2() const noexcept
{ return v2->adopted; }
void AttackComponent::advanceCapturePresentationAt (double now)
{ if (v2->enabled) v2->advanceClock (now); }
capture::PresentationStamp AttackComponent::capturePresentationStamp() const noexcept
{
    const auto& frozen = evidenceV2 ? *evidenceV2 : v2->adopted;
    return { frozen.header.snapshot_revision, frozen.revision, v2->enabled };
}
void AttackComponent::paintV2 (juce::Graphics& g)
{
    const auto shape = attack_v2::geometry (getWidth(), getHeight(), presentationContext, chosenBand == 0);
    attack_v2::paintV2 (g, *v2, shape, presentationContext);
    if (evidenceV2) attack_v2::paintEvidence (g, *evidenceV2, getLocalBounds(), presentationContext, evidenceScrollV2);
}
void AttackComponent::resizeV2()
{
    const auto shape = attack_v2::geometry (getWidth(), getHeight(), presentationContext, chosenBand == 0);
    if (selectionLaneV2 >= 0 && ! selectionPlotV2.isEmpty())
    {
        const auto plot = shape.lanes[static_cast<std::size_t> (selectionLaneV2)].axis;
        if (! plot.isEmpty())
        {
            for (auto& e : v2->selection.frozen)
            {
                const auto factor = static_cast<float> (plot.getWidth()) / selectionPlotV2.getWidth();
                e.x = plot.getX() + (e.x - selectionPlotV2.getX()) * factor; e.halfWidth *= factor;
            }
            selectionPlotV2 = plot;
            if (v2->selection.clustered && ! v2->selection.frozen.empty()) v2->selection.anchor = v2->selection.frozen[0].x;
        }
    }
    else v2->selection.reanchor (attack_v2::visibleEvents (*v2, shape));
}
bool AttackComponent::mouseDownV2 (juce::Point<int> point)
{
    const auto shape = attack_v2::geometry (getWidth(), getHeight(), presentationContext, chosenBand == 0);
    if (shape.live.contains (point)) { evidenceV2.reset(); v2->goLive(); repaint(); return true; }
    if (shape.evidence.contains (point))
    {
        if (evidenceV2) evidenceV2.reset(); else { evidenceV2 = v2->adopted; evidenceScrollV2 = 0; }
        repaint(); return true;
    }
    if (evidenceV2) { evidenceV2.reset(); repaint(); return true; }
    for (std::size_t i = 0; i < shape.bands.size(); ++i)
        if (shape.bands[i].contains (point)) { setBand (static_cast<std::uint8_t> (i)); return true; }
    if (shape.view.contains (point)) { setOverlayMode (! overlayMode); return true; }
    if (v2->selection.clustered && (shape.previous.contains (point) || shape.next.contains (point)))
    { v2->selection.cycle (shape.next.contains (point)); v2->changedSelection(); repaint(); return true; }
    if (shape.caption.contains (point) && v2->needsSingle() && v2->selection.selected)
    { v2->selection.live = false; v2->changedSelection(); repaint(); return true; }
    if (chosenBand != 0 && v2->cohort && v2->cohort->header.target == v2->target)
        for (std::size_t i = 0; i < shape.lanes.size(); ++i)
            if (! shape.lanes[i].axis.isEmpty() && shape.lanes[i].axis.contains (point))
            {
                const auto scale = attack_lanes::scaleFor (attack_lanes::bandLanes[i], v2->target == KIRIN_TARGET_DELTA);
                std::vector<attack_v2::LocatedEvent> dots;
                for (std::size_t hit = 0; hit < v2->cohort->cohort_count; ++hit)
                {
                    const auto& e = v2->cohort->evidence[hit][i];
                    if (! e.has_interval) continue;
                    const auto end = e.interval.lower.kind == KIRIN_ENDPOINT_FINITE ? e.interval.lower : e.interval.upper;
                    if (end.kind != KIRIN_ENDPOINT_FINITE) continue;
                    const auto plot = shape.lanes[i].axis;
                    const auto location = [&] (double value) { return plot.getX() + (plot.getWidth() - 1)
                        * static_cast<float> (std::clamp ((value - scale.minimum) / (scale.maximum - scale.minimum), 0.0, 1.0)); };
                    const auto left = location (end.value);
                    const auto right = e.interval.lower.kind == KIRIN_ENDPOINT_FINITE && e.interval.upper.kind == KIRIN_ENDPOINT_FINITE
                        ? location (e.interval.upper.value) : left;
                    dots.push_back ({ v2->cohort->events[hit], (left + right) * .5f, std::abs (right - left) * .5f });
                }
                selectionLaneV2 = static_cast<int> (i); selectionPlotV2 = shape.lanes[i].axis;
                v2->selection.begin (dots, static_cast<float> (point.x));
                if (v2->selection.live) v2->goLive(); else v2->changedSelection();
                repaint(); return true;
            }
    if (! shape.history.contains (point)) return false;
    selectionLaneV2 = -1; selectionPlotV2 = {};
    v2->selection.begin (attack_v2::visibleEvents (*v2, shape), static_cast<float> (point.x));
    if (v2->selection.live) v2->goLive(); else v2->changedSelection();
    repaint(); return true;
}
void AttackComponent::mouseDragV2 (juce::Point<int> point)
{
    const auto old = v2->selection.selected;
    v2->selection.drag (static_cast<float> (point.x));
    if (v2->selection.selected && (! old || ! attack_v2::sameEvent (*old, *v2->selection.selected)))
        v2->changedSelection();
    repaint();
}
void AttackComponent::mouseUp (const juce::MouseEvent&)
{ if (v2->enabled) v2->selection.dragging = false; }
void AttackComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (v2->enabled && evidenceV2)
    { evidenceScrollV2 = std::clamp (evidenceScrollV2 - juce::roundToInt (wheel.deltaY * 120), 0, 1600); repaint(); }
}
bool AttackComponent::keyPressedV2 (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::endKey) { evidenceV2.reset(); v2->goLive(); repaint(); return true; }
    if (key == juce::KeyPress::escapeKey)
    {
        if (evidenceV2) evidenceV2.reset(); else v2->selection.escape();
        repaint(); return true;
    }
    if (key.getTextCharacter() == 'i' || key.getTextCharacter() == 'I')
    {
        if (evidenceV2) evidenceV2.reset(); else { evidenceV2 = v2->adopted; evidenceScrollV2 = 0; }
        repaint(); return true;
    }
    if (evidenceV2 && (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey))
    { evidenceScrollV2 = std::clamp (evidenceScrollV2 + (key == juce::KeyPress::downKey ? 28 : -28), 0, 1600); repaint(); return true; }
    if (key != juce::KeyPress::leftKey && key != juce::KeyPress::rightKey && key != juce::KeyPress::homeKey) return false;
    std::vector<KirinSnapshotEventKey> window;
    const auto shape = attack_v2::geometry (getWidth(), getHeight(), presentationContext, chosenBand == 0);
    if (v2->selection.dragging)
        for (const auto& e : v2->selection.frozen) window.push_back (e.key);
    else if (chosenBand == 0)
        for (const auto& e : attack_v2::visibleEvents (*v2, shape)) window.push_back (e.key);
    else
    {
        // Ordinary keyboard LOCK navigates the current window; the visible pointer cohort stays
        // at its adopted cutoff. Cluster and an active drag retain their pointer-down keys.
        const auto& h = v2->navigationHeader();
        for (const auto& e : v2->events)
            if (e.event_sample <= h.cutoff_sample
                && h.cutoff_sample - e.event_sample <= static_cast<std::int64_t> (h.source.sample_rate) * 6) window.push_back (e);
        if (window.size() > 8) window.erase (window.begin(), window.end() - 8);
    }
    const auto old = v2->selection.selected; const bool wasLive = v2->selection.live;
    v2->selection.adjacent (window, key == juce::KeyPress::rightKey, key == juce::KeyPress::homeKey);
    if (wasLive != v2->selection.live || old.has_value() != v2->selection.selected.has_value()
        || (old && v2->selection.selected && ! attack_v2::sameEvent (*old, *v2->selection.selected))) v2->changedSelection();
    repaint(); return true;
}
}
