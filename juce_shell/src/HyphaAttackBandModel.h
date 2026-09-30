#pragma once

#include <cmath>
#include <cstdint>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackLaneModel.h"

// The band lanes' model (B-1097, B-1098). It is built over the whole-signal lanes' own hits:
// the same count, order, samples and selectability, so the band lanes cannot show other columns
// than ALL, count other events, or lose a selection when the band changes. Each hit's band
// record is found by the lanes' key (the engine keys it the same way); none yet reads "--".
//
// Every cell states what the engine measured, never an inference: a value, a bound (ATT inside
// the band's resolution, D4; REL past the measured tail; LEVEL against a silent side), or why
// there is none. Paired with a PRE that measures the band, each lane is POST - PRE of one hit
// measured at the PRE onset; otherwise POST's own values, with DELAY saying why it needs PRE.
namespace hypha::attack_band
{
using attack_lanes::Cell;
using attack_lanes::Hit;
using attack_lanes::index;
using attack_lanes::Lane;
using attack_lanes::measured;
using attack_lanes::Model;
using attack_lanes::Reason;
using attack_lanes::withheld;

static_assert (KIRIN_ATTACK_BAND_BATCH_CAPACITY == KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY);
static_assert (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY >= KIRIN_ATTACK_DETAIL_BATCH_CAPACITY);

enum class PreBand : std::uint8_t
{
    off = KIRIN_ATTACK_BAND_PRE_OFF,
    same = KIRIN_ATTACK_BAND_PRE_SAME,
    waiting = KIRIN_ATTACK_BAND_PRE_WAITING,
    predates = KIRIN_ATTACK_BAND_PRE_PREDATES,
};

constexpr float floorDbfs = KIRIN_ATTACK_BAND_PRESENCE_FLOOR_DBFS;

// The batch describes `band` for this run: another band (the engine has not caught up with a new
// choice), another run or another rate describes nothing here.
inline bool current (const KirinAttackBandBatch& batch, std::uint8_t band,
                     std::uint64_t generation, std::uint32_t rate) noexcept
{
    return band != 0 && batch.band == band && batch.generation == generation
        && batch.sample_rate == rate;
}

// The record keyed `sample`, or nullptr. The engine gives the hits oldest first.
inline const KirinAttackBandHit* findHit (const KirinAttackBandBatch& batch, std::uint8_t band,
                                          std::uint64_t generation, std::uint32_t rate,
                                          std::int64_t sample) noexcept
{
    if (! current (batch, band, generation, rate))
        return nullptr;
    const auto count = batch.count < KIRIN_ATTACK_BAND_BATCH_CAPACITY
        ? batch.count : static_cast<std::uint32_t> (KIRIN_ATTACK_BAND_BATCH_CAPACITY);
    std::uint32_t low = 0, high = count;
    while (low < high)
    {
        const auto middle = low + (high - low) / 2;
        if (batch.hits[middle].event_sample < sample) low = middle + 1;
        else high = middle;
    }
    return low < count && batch.hits[low].event_sample == sample ? &batch.hits[low] : nullptr;
}

inline PreBand preBandOf (const KirinAttackBandBatch& batch, std::uint8_t band) noexcept
{
    // A batch that has not caught up with the choice is PRE's side not being there yet.
    return batch.band == band ? static_cast<PreBand> (batch.pre_band) : PreBand::waiting;
}

// POST - PRE while paired and PRE measures the band or is about to; POST's own values otherwise.
inline bool deltaFor (const Model& lanes, PreBand pre) noexcept
{
    return lanes.delta && (pre == PreBand::same || pre == PreBand::waiting);
}

inline Cell bound (float value) noexcept
{
    return std::isfinite (value) ? Cell { value, Reason::atLeast } : Cell {};
}

inline Cell attackCell (float value, float resolutionMs) noexcept
{
    if (! std::isfinite (value))
        return {};
    return std::abs (value) < resolutionMs ? Cell { resolutionMs, Reason::withinResolution }
                                           : measured (value);
}

inline bool rises (const KirinAttackBandSide& side) noexcept
{
    return side.state == KIRIN_ATTACK_BAND_SIDE_RISES;
}

inline bool timed (const KirinAttackBandSide& side) noexcept
{
    return rises (side) && side.arrival_state == KIRIN_ATTACK_BAND_ARRIVAL_AT;
}

inline Cell releaseDelta (const KirinAttackBandSide& pre, const KirinAttackBandSide& post) noexcept
{
    const auto state = [] (const KirinAttackBandSide& side) { return side.release_state; };
    if (state (pre) == KIRIN_ATTACK_BAND_RELEASE_NEXT_HIT
        || state (post) == KIRIN_ATTACK_BAND_RELEASE_NEXT_HIT)
        return withheld (Reason::nextHit);
    const auto change = post.release_ms - pre.release_ms;
    const bool preTimed = state (pre) == KIRIN_ATTACK_BAND_RELEASE_AT;
    const bool postTimed = state (post) == KIRIN_ATTACK_BAND_RELEASE_AT;
    if (preTimed && postTimed)
        return measured (change);
    // One side rings past the measured tail: the change is a bound when its sign is certain.
    if (preTimed && change > 0.0f)
        return bound (change);  // POST at least this much longer
    if (postTimed && change < 0.0f)
        return bound (change);  // POST at least this much shorter
    return withheld (Reason::longTail);
}

inline void fillDelta (Hit& hit, const KirinAttackBandHit& source, bool waiting,
                       float resolutionMs) noexcept
{
    const auto all = [&hit] (Reason reason) { hit.cells.fill (withheld (reason)); };
    if (source.kind != 0)
        return all (Reason::noMatch);
    const auto& pre = source.pre;
    const auto& post = source.post;
    if (pre.state == KIRIN_ATTACK_BAND_SIDE_NOT_KEPT || post.state == KIRIN_ATTACK_BAND_SIDE_NOT_KEPT)
        return all (Reason::notMeasured);
    if (waiting || pre.state == KIRIN_ATTACK_BAND_SIDE_PENDING
        || post.state == KIRIN_ATTACK_BAND_SIDE_PENDING)
        return all (Reason::missing);
    const bool preSilent = pre.state == KIRIN_ATTACK_BAND_SIDE_SILENT;
    const bool postSilent = post.state == KIRIN_ATTACK_BAND_SIDE_SILENT;
    if (preSilent && postSilent)
        return all (Reason::noSound);
    if (pre.state == KIRIN_ATTACK_BAND_SIDE_RINGS_ON || post.state == KIRIN_ATTACK_BAND_SIDE_RINGS_ON)
        return all (Reason::ringing);
    auto& level = hit.cells[index (Lane::level)];
    if (preSilent || postSilent)
    {
        // The band appears or disappears across the chain: the change is at least the gap to
        // the floor the silent side is below.
        const auto side = withheld (preSilent ? Reason::preNoSound : Reason::postNoSound);
        hit.cells.fill (side);
        level = preSilent ? bound (post.level_dbfs - floorDbfs) : bound (floorDbfs - pre.level_dbfs);
        return;
    }
    level = measured (post.level_dbfs - pre.level_dbfs);
    const bool started = timed (pre) && timed (post);
    hit.cells[index (Lane::delay)] = started ? measured (post.arrival_ms - pre.arrival_ms)
                                             : withheld (Reason::ringing);
    hit.cells[index (Lane::attackTime)] = started
        ? attackCell (post.attack_ms - pre.attack_ms, resolutionMs) : withheld (Reason::ringing);
    hit.cells[index (Lane::release)] = releaseDelta (pre, post);
}

inline void fillAbsolute (Hit& hit, const KirinAttackBandHit& source, Reason delayReason,
                          float resolutionMs) noexcept
{
    const auto& side = source.post;
    const auto rest = [&hit] (Reason reason) {
        for (const auto lane : { Lane::attackTime, Lane::release, Lane::level })
            hit.cells[index (lane)] = withheld (reason);
    };
    if (side.state == KIRIN_ATTACK_BAND_SIDE_ABSENT)
        return hit.cells.fill (withheld (Reason::noMatch));
    hit.cells[index (Lane::delay)] = withheld (delayReason);
    switch (side.state)
    {
        case KIRIN_ATTACK_BAND_SIDE_PENDING:  return rest (Reason::missing);
        case KIRIN_ATTACK_BAND_SIDE_NOT_KEPT: return rest (Reason::notMeasured);
        case KIRIN_ATTACK_BAND_SIDE_SILENT:   return rest (Reason::noSound);
        case KIRIN_ATTACK_BAND_SIDE_RINGS_ON: return rest (Reason::ringing);
        default: break;
    }
    hit.cells[index (Lane::attackTime)] = timed (side) ? attackCell (side.attack_ms, resolutionMs)
                                                       : withheld (Reason::ringing);
    hit.cells[index (Lane::release)] =
        side.release_state == KIRIN_ATTACK_BAND_RELEASE_AT ? measured (side.release_ms)
      : side.release_state == KIRIN_ATTACK_BAND_RELEASE_AT_LEAST ? bound (side.release_ms)
      : withheld (Reason::nextHit);
    hit.cells[index (Lane::level)] = measured (side.level_dbfs);
}

// The band lanes for exactly `lanes`' hits.
inline void build (Model& band, const Model& lanes, const KirinAttackBandBatch& batch,
                   std::uint8_t chosen, std::uint64_t generation, std::uint32_t rate) noexcept
{
    const auto pre = preBandOf (batch, chosen);
    band.delta = deltaFor (lanes, pre);
    band.count = chosen != 0 ? lanes.count : 0;
    const auto resolutionMs = static_cast<float> (batch.resolution_micros) / 1'000.0f;
    for (std::uint32_t item = 0; item < band.count; ++item)
    {
        const auto& lane = lanes.hits[item];
        auto& hit = band.hits[item];
        hit = {};
        hit.sample = lane.sample;
        hit.selectable = lane.selectable;
        hit.resolutionMs = resolutionMs;
        // The whole-signal sides name PRE ONLY / POST ONLY / NO PAIR the same way.
        hit.pre.available = lane.pre.available;
        hit.post.available = lane.post.available;
        const auto* source = findHit (batch, chosen, generation, rate, lane.sample);
        if (source == nullptr)
            hit.cells.fill (withheld (Reason::missing));
        else if (band.delta)
            fillDelta (hit, *source, pre == PreBand::waiting, resolutionMs);
        else
            fillAbsolute (hit, *source, lanes.delta ? Reason::updatePre : Reason::noPair,
                          resolutionMs);
    }
}

// How far a hit's band cells have come: values (or a stated fact), not measured, or not yet.
enum class Readiness : std::uint8_t
{
    pending,
    notMeasured,
    stated,
};

inline Readiness readiness (const Hit& hit) noexcept
{
    const auto reason = hit.cells[index (Lane::level)].reason;
    return reason == Reason::missing ? Readiness::pending
         : reason == Reason::notMeasured ? Readiness::notMeasured : Readiness::stated;
}
}
