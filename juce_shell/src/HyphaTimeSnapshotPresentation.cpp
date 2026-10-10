#include "HyphaTimeSnapshotPresentation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hypha::time_snapshot
{
namespace
{
bool validTarget (std::uint8_t target) noexcept
{
    return target == KIRIN_TIME_PRE || target == KIRIN_TIME_POST || target == KIRIN_TIME_DELTA;
}

bool validComponent (const KirinTimeComponentV2& component,
                     const std::vector<KirinTimeHistoryEntryV2>& history) noexcept
{
    const auto& current = component.current;
    const double lifetimeMs = current.target == KIRIN_TIME_DELTA ? 1000.0 : 400.0;
    if (! validTarget (current.target) || current.state < KIRIN_TIME_CURRENT_LIVE
        || current.state > KIRIN_TIME_CURRENT_WAITING || current.finite_mask > 63u
        || component.history_count != history.size() || history.size() > KIRIN_TIME_HISTORY_CAPACITY)
        return false;
    if (current.state == KIRIN_TIME_CURRENT_LIVE
        && (! std::isfinite (current.remaining_ms) || current.remaining_ms < 0.0
            || current.remaining_ms > lifetimeMs || ! std::isfinite (current.completion_age_ms)
            || current.completion_age_ms < 0.0))
        return false;
    return std::all_of (history.begin(), history.end(), [] (const auto& entry)
    {
        return entry.first_observed <= entry.last_observed && entry.total_count > 0u
            && std::all_of (std::begin (entry.valid_count), std::end (entry.valid_count),
                            [&] (auto count) { return count <= entry.total_count; });
    });
}

bool sameCompletion (const KirinTimeComponentV2& a, const KirinTimeComponentV2& b) noexcept
{
    return sameSpan (a.current.span, b.current.span) && a.current.cutoff == b.current.cutoff
        && a.current.run == b.current.run && a.current.endpoint == b.current.endpoint
        && a.current.clock == b.current.clock;
}

Component adopt (const KirinTimeComponentV2& facts, std::vector<KirinTimeHistoryEntryV2> history,
                 const Component* previous, double pollStartedMs)
{
    Component result { facts, std::move (history), 0.0 };
    if (facts.current.state == KIRIN_TIME_CURRENT_LIVE)
    {
        result.deadlineMs = pollStartedMs + facts.current.remaining_ms;
        // Re-polling or rejoining one completed slot cannot restart its lifetime.
        if (previous != nullptr && sameCompletion (previous->facts, facts)
            && previous->deadlineMs > 0.0)
            result.deadlineMs = std::min (result.deadlineMs, previous->deadlineMs);
    }
    return result;
}

bool expire (Component& component, double nowMs) noexcept
{
    if (component.facts.current.state != KIRIN_TIME_CURRENT_LIVE)
        return false;
    auto& current = component.facts.current;
    const auto remaining = std::max (0.0, component.deadlineMs - nowMs);
    current.completion_age_ms += std::max (0.0, current.remaining_ms - remaining);
    current.remaining_ms = std::min (current.remaining_ms, remaining);
    if (nowMs < component.deadlineMs) return false;
    component.facts.current.state = KIRIN_TIME_CURRENT_EXPIRED;
    component.facts.current.finite_mask = 0u;
    component.facts.current.remaining_ms = 0.0;
    return true;
}

bool stopCurrent (Component& component) noexcept
{
    auto& facts = component.facts;
    const auto held = static_cast<std::uint8_t> (! component.history.empty());
    const bool changed = facts.current.state != KIRIN_TIME_CURRENT_STOPPED
        || facts.current.finite_mask != 0u || facts.history_hold != held;
    facts.current.state = KIRIN_TIME_CURRENT_STOPPED;
    facts.current.finite_mask = 0u;
    facts.current.remaining_ms = 0.0;
    facts.history_hold = held;
    return changed;
}

void retireComponent (Component& component) noexcept
{
    component.facts.current.state = KIRIN_TIME_CURRENT_WAITING;
    component.facts.current.finite_mask = 0u;
    component.facts.current.remaining_ms = 0.0;
    component.deadlineMs = 0.0;
    component.history.clear();
    component.facts.history_count = 0u;
    component.facts.history_hold = 0u;
}
}

bool sameSpan (const KirinTimeSourceSpanV2& a, const KirinTimeSourceSpanV2& b) noexcept
{
    return a.epoch == b.epoch && a.incarnation == b.incarnation
        && a.generation == b.generation && a.token == b.token
        && a.sample_rate == b.sample_rate && a.channels == b.channels;
}

bool Component::currentAvailable (Metric metric) const noexcept
{
    const auto index = static_cast<unsigned> (metric);
    return facts.current.state == KIRIN_TIME_CURRENT_LIVE
        && (facts.current.finite_mask & (1u << index)) != 0u
        && std::isfinite (facts.current.values[index]);
}

double Component::value (Metric metric) const noexcept
{
    return currentAvailable (metric) ? facts.current.values[static_cast<unsigned> (metric)]
                                    : std::numeric_limits<double>::quiet_NaN();
}

bool Presentation::apply (const KirinTimeSnapshotV2& packet,
                           std::vector<KirinTimeHistoryEntryV2> mainHistory,
                           std::vector<KirinTimeHistoryEntryV2> psrHistory,
                           double pollStartedMs, double nowMs, bool showPsr)
{
    if (packet.version != KIRIN_TIME_SNAPSHOT_VERSION || packet.struct_size != sizeof (packet)
        || packet.range_start > packet.local_cutoff || ! std::isfinite (pollStartedMs)
        || ! std::isfinite (nowMs) || nowMs < pollStartedMs
        || ! validComponent (packet.main, mainHistory)
        || (showPsr && ! validComponent (packet.psr, psrHistory))
        || (! showPsr && (! psrHistory.empty() || packet.psr.history_count != 0u)))
        return false;
    if (havePacket && sameSpan (accepted.post_span, packet.post_span)
        && packet.local_cutoff < accepted.local_cutoff)
        return false;
    auto main = adopt (packet.main, std::move (mainHistory), havePacket ? &mainComponent : nullptr,
                       pollStartedMs);
    auto psr = showPsr ? adopt (packet.psr, std::move (psrHistory),
                                havePacket && showPsrLane ? &psrComponent : nullptr, pollStartedMs)
                      : Component {};
    if (knownInactive)
    {
        stoppedCutoff = haveStopFence && sameSpan (stoppedSource, packet.post_span)
            ? std::max (stoppedCutoff, packet.local_cutoff) : packet.local_cutoff;
        stoppedSource = packet.post_span;
        haveStopFence = true;
    }
    const bool fenced = haveStopFence && sameSpan (stoppedSource, packet.post_span);
    if (knownInactive || (fenced && main.facts.current.state == KIRIN_TIME_CURRENT_LIVE
                         && main.facts.current.cutoff <= stoppedCutoff))
        stopCurrent (main);
    if (showPsr && (knownInactive || (fenced && psr.facts.current.state == KIRIN_TIME_CURRENT_LIVE
                                      && psr.facts.current.cutoff <= stoppedCutoff)))
        stopCurrent (psr);
    accepted = packet;
    mainComponent = std::move (main);
    psrComponent = std::move (psr);
    havePacket = true;
    showPsrLane = showPsr;
    ++presentationRevision;
    advance (nowMs);
    return true;
}

bool Presentation::advance (double nowMs) noexcept
{
    if (! havePacket || ! std::isfinite (nowMs)) return false;
    const bool changedMain = expire (mainComponent, nowMs);
    const bool changedPsr = showPsrLane && expire (psrComponent, nowMs);
    if (changedMain || changedPsr) ++presentationRevision;
    return changedMain || changedPsr;
}

bool Presentation::observeInput (bool active) noexcept
{
    knownInactive = ! active;
    if (active || ! havePacket) return false;
    stoppedCutoff = haveStopFence && sameSpan (stoppedSource, accepted.post_span)
        ? std::max (stoppedCutoff, accepted.local_cutoff) : accepted.local_cutoff;
    stoppedSource = accepted.post_span;
    haveStopFence = true;
    const bool changedMain = stopCurrent (mainComponent);
    const bool changedPsr = showPsrLane && stopCurrent (psrComponent);
    if (changedMain || changedPsr) ++presentationRevision;
    return changedMain || changedPsr;
}

void Presentation::selectMainTarget (std::uint8_t target) noexcept
{
    if (! validTarget (target)) return;
    retireComponent (mainComponent);
    mainComponent.facts.current.target = target;
    mainComponent.facts.reason = KIRIN_TIME_REASON_NONE;
    ++presentationRevision;
}

void Presentation::retire (bool localSourceChanged) noexcept
{
    if (localSourceChanged)
    {
        retireComponent (mainComponent);
        retireComponent (psrComponent);
    }
    else
    {
        if (mainComponent.facts.current.target == KIRIN_TIME_DELTA) retireComponent (mainComponent);
        if (psrComponent.facts.current.target == KIRIN_TIME_DELTA) retireComponent (psrComponent);
    }
    ++presentationRevision;
}

double Presentation::normalizedX (std::uint64_t observed) const noexcept
{
    if (! havePacket || accepted.local_cutoff <= accepted.range_start) return 1.0;
    if (observed <= accepted.range_start) return 0.0;
    if (observed >= accepted.local_cutoff) return 1.0;
    return static_cast<double> (observed - accepted.range_start)
         / static_cast<double> (accepted.local_cutoff - accepted.range_start);
}

unsigned rangeIndex (Metric metric) noexcept
{
    return metric == Metric::psr ? 4u : metric == Metric::correlation ? 3u
        : static_cast<unsigned> (metric);
}

bool connects (const KirinTimeHistoryEntryV2& previous,
               const KirinTimeHistoryEntryV2& current, unsigned index) noexcept
{
    return index < 5u && current.connects_previous != 0u && current.epoch == previous.epoch
        && current.generation == previous.generation && current.run == previous.run
        && current.segment == previous.segment && current.clock == previous.clock
        && previous.last_observed < current.first_observed
        && previous.valid_count[index] > 0u && current.valid_count[index] > 0u
        && std::isfinite (previous.ranges[index].mean) && std::isfinite (current.ranges[index].mean);
}
}
