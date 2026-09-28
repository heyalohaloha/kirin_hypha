#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackLaneModel.h"

// The band lanes' model (B-1097). Paired with a PRE that has sent the band, each lane is the
// exact POST - PRE difference of one hit's two band measures (POST measured at the PRE onset over
// the PRE span); otherwise POST's own band values, with DELAY withheld since it is a difference
// only. ATT within the band's time resolution (one period of its centre) is not stated finer
// than that bound (D4): the cell keeps the bound and says so.
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

static_assert (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY >= KIRIN_ATTACK_BAND_BATCH_CAPACITY);

inline attack_lanes::Side sideFor (const KirinAttackBandSide& side) noexcept
{
    attack_lanes::Side result;
    if (side.available == 0)
        return result;
    constexpr auto none = std::numeric_limits<float>::quiet_NaN();
    result.available = true;
    result.complete = true;
    result.values[index (Lane::attackTime)] = side.attack_available != 0 ? side.attack_ms : none;
    result.values[index (Lane::release)] = side.release_available != 0 ? side.release_ms : none;
    result.values[index (Lane::level)] = side.level_dbfs;
    return result;
}

inline Cell attackCell (float value, float resolutionMs) noexcept
{
    if (! std::isfinite (value))
        return withheld (Reason::missing);
    return std::abs (value) < resolutionMs ? Cell { resolutionMs, Reason::belowResolution }
                                           : measured (value);
}

inline void fillDelta (Hit& hit, const KirinAttackBandHit& source) noexcept
{
    if (source.kind != 0)
        return hit.cells.fill (withheld (Reason::noMatch));
    if (! hit.pre.available || ! hit.post.available)
        return hit.cells.fill (withheld (Reason::missing));
    const auto pre = [&hit] (Lane lane) { return hit.pre.values[index (lane)]; };
    const auto post = [&hit] (Lane lane) { return hit.post.values[index (lane)]; };
    const bool ringing = source.pre.arrival_available == 0 || source.post.arrival_available == 0;
    hit.cells[index (Lane::delay)] = source.delay_available != 0 ? measured (source.delay_ms)
                                   : withheld (ringing ? Reason::ringing : Reason::missing);
    hit.cells[index (Lane::attackTime)] = ringing ? withheld (Reason::ringing)
        : attackCell (post (Lane::attackTime) - pre (Lane::attackTime), hit.resolutionMs);
    const auto release = post (Lane::release) - pre (Lane::release);
    hit.cells[index (Lane::release)] = std::isfinite (release) ? measured (release)
                                                               : withheld (Reason::nextHit);
    hit.cells[index (Lane::level)] = measured (post (Lane::level) - pre (Lane::level));
}

inline void fillAbsolute (Hit& hit, const KirinAttackBandHit& source, bool paired) noexcept
{
    hit.cells[index (Lane::delay)] = withheld (paired ? Reason::noPreBand : Reason::noMatch);
    if (! hit.post.available)
    {
        for (const auto lane : { Lane::attackTime, Lane::release, Lane::level })
            hit.cells[index (lane)] = withheld (Reason::missing);
        return;
    }
    hit.cells[index (Lane::attackTime)] = source.post.arrival_available == 0
        ? withheld (Reason::ringing)
        : attackCell (hit.post.values[index (Lane::attackTime)], hit.resolutionMs);
    hit.cells[index (Lane::release)] = source.post.release_available != 0
        ? measured (hit.post.values[index (Lane::release)]) : withheld (Reason::nextHit);
    hit.cells[index (Lane::level)] = measured (hit.post.values[index (Lane::level)]);
}

// `band` is the band the view shows. A batch measured in another band (the engine has not
// caught up with a new choice) contributes nothing, and neither does another generation or rate.
inline void build (Model& model, const KirinAttackBandBatch& batch, std::uint8_t band,
                   std::uint64_t generation, std::uint32_t rate, bool paired) noexcept
{
    model.count = 0;
    model.delta = paired && batch.pre_band_available != 0;
    if (band == 0 || batch.band != band)
        return;
    const auto count = batch.count < KIRIN_ATTACK_BAND_BATCH_CAPACITY
        ? batch.count : static_cast<std::uint32_t> (KIRIN_ATTACK_BAND_BATCH_CAPACITY);
    for (std::uint32_t item = 0; item < count; ++item)
    {
        const auto& source = batch.hits[item];
        if (source.generation != generation || source.sample_rate != rate || source.band != band)
            continue;
        auto& hit = model.hits[model.count++];
        hit = {};
        hit.sample = source.event_sample;
        hit.selectable = source.post.available != 0;
        hit.resolutionMs = static_cast<float> (source.resolution_micros) / 1'000.0f;
        hit.pre = sideFor (source.pre);
        hit.post = sideFor (source.post);
        if (model.delta)
            fillDelta (hit, source);
        else
            fillAbsolute (hit, source, paired);
    }
}

// The engine's hit behind a model hit, for the HEAD / TAIL panes.
inline const KirinAttackBandHit* findHit (const KirinAttackBandBatch& batch, std::uint8_t band,
                                          std::int64_t sample, std::uint64_t generation,
                                          std::uint32_t rate) noexcept
{
    if (band == 0 || batch.band != band)
        return nullptr;
    const auto count = batch.count < KIRIN_ATTACK_BAND_BATCH_CAPACITY
        ? batch.count : static_cast<std::uint32_t> (KIRIN_ATTACK_BAND_BATCH_CAPACITY);
    for (std::uint32_t item = 0; item < count; ++item)
    {
        const auto& hit = batch.hits[item];
        if (hit.event_sample == sample && hit.generation == generation
            && hit.sample_rate == rate && hit.band == band)
            return &hit;
    }
    return nullptr;
}
}
