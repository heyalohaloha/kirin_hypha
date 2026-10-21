#include "HyphaAttackV2Presentation.h"
#include "HyphaAttackUiContract.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace hypha::attack_v2
{
namespace
{
bool sameNavigationAuthority (const KirinSnapshotHeader& a, const KirinSnapshotHeader& b)
{ return sameSource (a.source, b.source) && a.authority_revision == b.authority_revision && a.target == b.target; }
bool sameBinding (const KirinSnapshotHeader& a, const KirinSnapshotHeader& b)
{ return sameSource (a.source, b.source) && a.authority_revision == b.authority_revision; }
}
void State::stamp()
{
    if (adopted.viewport != clock.position() || adopted.factsCutoff != clock.ceiling()
        || adopted.clock != clock.status() || adopted.live != selection.live || adopted.inputActive != activeInput)
        adopted.revision = nextRevision++;
    adopted.viewport = clock.position(); adopted.factsCutoff = clock.ceiling(); adopted.clock = clock.status();
    adopted.live = selection.live; adopted.inputActive = activeInput;
}
void State::apply (Presentation next)
{
    if (next.summary)
    {
        cohort = next.summary;
        if (band == 0 && cohort->cohort_count > 0
            && (cohort->has_representative_pre || cohort->has_representative_post))
        {
            const auto& key = cohort->events[cohort->cohort_count - 1];
            if (livePresentation && livePresentation->representative
                && sameEvent (key, livePresentation->representative->key))
                next.representative = livePresentation->representative;
            else
            {
                auto loupe = std::make_shared<RepresentativeLoupe>();
                loupe->key = key; loupe->packet = cohort;
                next.representative = std::move (loupe);
            }
        }
        if (cohort->cohort_count > 0 && cohort->newer_unfinished_count > 0 && livePresentation)
            for (std::size_t i = 0; i < next.lanes.size(); ++i)
                if (cohort->lanes[i].class_count[KIRIN_SCALAR_EXACT] + cohort->lanes[i].class_count[KIRIN_SCALAR_BOUND] > 0
                    && (!next.lanes[i].number.valid || next.lanes[i].scope == Scope::noScalar
                        || next.lanes[i].scope == Scope::confirmedSubset))
                {
                    const auto& old = livePresentation->lanes[i];
                    if (old.heldNumber) next.lanes[i].heldNumber = old.heldNumber;
                    else if (old.number.valid && old.scope != Scope::confirmedSubset && old.scope != Scope::noScalar)
                        next.lanes[i].heldNumber = old.number;
                }
    }
    next.details = adopted.details; next.revision = nextRevision++;
    adopted = std::move (next); stamp();
    if (adopted.summary && selection.live) livePresentation = adopted;
}
Presentation State::placeholder (std::uint8_t reason, bool selectedSingle) const
{
    Presentation next;
    next.header = navHeader; next.header.band = band; next.header.target = target;
    for (std::size_t i = 0; i < next.lanes.size(); ++i)
    {
        auto& lane = next.lanes[i]; lane.reason = reason;
        lane.scope = selectedSingle ? Scope::single : Scope::noScalar;
        lane.count = selectedSingle ? 1 : 0;
        if (selectedSingle) lane.counts[KIRIN_SCALAR_PENDING] = 1;
        lane.number.unit = band != 0 ? (i == 3 ? target == KIRIN_TARGET_DELTA ? "dB" : "dBFS" : "ms")
            : i == 3 ? "acum" : i == 1 && target != KIRIN_TARGET_DELTA ? "dBFS" : "dB";
    }
    return next;
}
void State::begin()
{
    retire(); enabled = true;
    adopted = placeholder (KIRIN_REASON_WAITING_SERVICE); adopted.revision = nextRevision++;
    stamp();
}
void State::retire (std::uint8_t reason)
{
    if (token != 0 && cancel) cancel (token);
    token = 0; requested.reset(); pending.reset(); cohort.reset(); livePresentation.reset(); appliedMs = -1;
    selection.clear(); events.clear(); waveform = {}; preWaveform = {}; navHeader = {};
    clock.reset(); awaitingBandNavigation = false; activeInput = false;
    adopted = placeholder (reason); adopted.revision = nextRevision++;
}
bool State::navigate (const KirinSnapshotHeader& h, const std::vector<KirinSnapshotEventKey>& next,
                       const KirinAttackWaveformBatch& wave, const KirinAttackWaveformBatch& pre,
                       double now, bool realtime)
{
    if (! validHeader (h) || h.kind != 4 || h.band != 0 || h.struct_size != sizeof (KirinAttackNavigationV2)
        || next.size() > KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY
        || ! std::isfinite (now) || wave.count > KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY
        || pre.count > KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY) return false;
    for (std::size_t i = 0; i < next.size(); ++i)
        if (next[i].event_sample > h.cutoff_sample || next[i].source.sample_rate != h.source.sample_rate
            || (i > 0 && next[i].event_sample < next[i - 1].event_sample)) return false;
    const bool targetChanged = enabled && h.target != target;
    if (enabled && navHeader.source.generation != 0 && ((! sameBinding (h, navHeader))
        || h.cutoff_sample < navHeader.cutoff_sample)) retire();
    const bool finishBandChange = awaitingBandNavigation;
    awaitingBandNavigation = false;
    enabled = true; navHeader = h;
    const bool fixed = holdingFinishedSingle();
    if (!fixed) target = h.target;
    if (targetChanged && !fixed)
    {
        if (token != 0 && cancel) cancel (token);
        token = 0; requested.reset(); pending.reset(); livePresentation.reset(); appliedMs = -1;
        if (selection.live) cohort.reset(); // LOCK's fixed time candidates are target-independent.
        adopted = placeholder (KIRIN_REASON_WAITING_SERVICE); adopted.revision = nextRevision++;
    }
    realtimeInput = realtime; activeInput = h.signal_state == 1;
    events = next; waveform = wave; preWaveform = pre;
    clock.observe (h.cutoff_sample, h.source.sample_rate, now, h.signal_state == 1, realtime);
    const auto old = selection.selected;
    selection.latest (events);
    if (needsSingle() && ((!fixed && targetChanged) || finishBandChange || old.has_value() != selection.selected.has_value()
        || (old && selection.selected && ! sameEvent (*old, *selection.selected)))) changedSelection();
    tick (now);
    return true;
}
bool State::summary (const KirinAttackBandSummaryV2& packet, double now)
{
    if (awaitingBandNavigation || ! validSummary (packet) || packet.header.band != band || ! selection.live
        || (navHeader.source.generation != 0 && ! sameNavigationAuthority (packet.header, navHeader))) return false;
    if (adopted.summary && ! sameAuthority (packet.header, adopted.header))
    { retire(); enabled = true; return false; }
    // Reject an older stamp before a binding change clears the adopted packet.
    if (adopted.summary && packet.header.cutoff_sample < adopted.summary->header.cutoff_sample) return false;
    if (livePresentation && livePresentation->summary
        && !std::equal (std::begin (packet.comparison_binding), std::end (packet.comparison_binding),
                       std::begin (livePresentation->summary->comparison_binding)))
    {
        livePresentation.reset(); pending.reset(); cohort.reset(); appliedMs = -1;
        adopted = placeholder (KIRIN_REASON_SOURCE_CHANGED); adopted.revision = nextRevision++;
    }
    enabled = true;
    // Summary revision is a content fingerprint, not a sequence number. Late measurements at
    // the same cutoff are new facts even when their fingerprint is numerically smaller.
    if (adopted.summary && (packet.header.cutoff_sample < adopted.summary->header.cutoff_sample
        || packet.header.snapshot_revision == adopted.summary->header.snapshot_revision)) return false;
    if (pending && (packet.header.snapshot_revision == pending->header.snapshot_revision
        || packet.header.cutoff_sample < pending->header.cutoff_sample)) return false;
    pending = std::make_shared<const KirinAttackBandSummaryV2> (packet);
    tick (now);
    return true;
}
bool State::single (const KirinAttackSingleSnapshotV2& packet, double now)
{
    if (! validSingle (packet) || ! needsSingle() || ! requested || token == 0 || packet.request_token != token
        || ! sameEvent (packet.event, *requested) || packet.header.band != band
        || (navHeader.source.generation != 0
            && !(holdingFinishedSingle() ? sameBinding (packet.header, navHeader)
                                         : sameNavigationAuthority (packet.header, navHeader)))) return false;
    if (adopted.single && ! sameAuthority (packet.header, adopted.header))
    { retire(); enabled = true; return false; }
    if (packet.finish == KIRIN_FINISH_RETIRED && packet.reason == KIRIN_REASON_SOURCE_CHANGED)
    { retire(); enabled = true; return true; }
    if (adopted.single && (packet.measurement_revision <= adopted.single->measurement_revision
        || adopted.single->finish != KIRIN_FINISH_ACQUIRING)) return false;
    apply (singlePresentation (std::make_shared<const KirinAttackSingleSnapshotV2> (packet)));
    tick (now);
    return true;
}
void State::tick (double now)
{
    if (!std::isfinite (now)) return;
    clock.tick (now);
    if (selection.live && pending && (activeInput || !livePresentation)
        && (appliedMs < 0 || now - appliedMs >= attack_ui::liveAdoptionMs))
    {
        apply (summaryPresentation (std::move (pending))); pending.reset(); appliedMs = now;
    }
    requestSelected(); stamp();
}
void State::advanceClock (double now)
// Capture advances the adopted presentation without admitting packets or requesting measurement.
{ clock.tick (now); stamp(); }
void State::requestSelected()
{
    if (awaitingBandNavigation || ! needsSingle() || ! selection.selected || ! request) return;
    if (requested && sameEvent (*requested, *selection.selected)) return;
    KirinAttackSingleV2Request input {};
    input.version = 2; input.struct_size = sizeof (input); input.band = band; input.target = target;
    input.event = *selection.selected;
    std::uint64_t accepted = 0;
    const auto status = request (input, accepted);
    if (status == KIRIN_SNAPSHOT_BUSY) return; // Same intent, no token or output retirement.
    requested = input.event;
    if (status == KIRIN_SNAPSHOT_SUCCESS && accepted != 0) { token = accepted; return; }
    for (auto& lane : adopted.lanes)
    {
        lane.reason = status == KIRIN_SNAPSHOT_RETIRED ? KIRIN_REASON_NOT_KEPT : KIRIN_REASON_MAPPING;
        lane.counts = {}; lane.counts[KIRIN_SCALAR_UNKNOWN] = 1;
    }
    adopted.revision = nextRevision++;
}
void State::changedSelection()
{
    if (! awaitingBandNavigation && requested && selection.selected && adopted.single
        && sameEvent (*requested, *selection.selected) && adopted.header.band == band && adopted.header.target == target)
    { adopted.revision = nextRevision++; stamp(); return; }
    if (token != 0 && cancel) cancel (token);
    token = 0; requested.reset(); pending.reset();
    apply (placeholder (KIRIN_REASON_WAITING_SERVICE, selection.selected.has_value())); requestSelected();
}
void State::changeBand (std::uint8_t next)
{
    if (next == band) return;
    band = next; target = navHeader.target; selection.escape();
    if (token != 0 && cancel) cancel (token);
    token = 0; requested.reset(); pending.reset(); cohort.reset(); livePresentation.reset(); appliedMs = -1;
    adopted = placeholder (KIRIN_REASON_WAITING_SERVICE);
    awaitingBandNavigation = enabled && navHeader.source.generation != 0;
    adopted.revision = nextRevision++; stamp();
    if (needsSingle()) changedSelection();
}
void State::goLive()
{
    if (selection.live && adopted.live) { selection.escape(); stamp(); return; }
    if (token != 0 && cancel) cancel (token);
    token = 0; target = navHeader.target; requested.reset(); pending.reset(); selection.clear(); selection.latest (events);
    if (livePresentation && livePresentation->header.band == band
        && sameNavigationAuthority (livePresentation->header, navHeader))
    { adopted = *livePresentation; cohort = adopted.summary; }
    else
    { livePresentation.reset(); cohort.reset(); adopted = placeholder (KIRIN_REASON_WAITING_SERVICE); appliedMs = -1; }
    adopted.revision = nextRevision++; stamp();
}
void State::observeInput (bool active, double now)
{
    activeInput = active;
    clock.observe (navHeader.cutoff_sample, navHeader.source.sample_rate, now, active, realtimeInput);
    tick (now);
}
}
