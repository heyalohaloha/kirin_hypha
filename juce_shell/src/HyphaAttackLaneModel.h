#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackUiContract.h"

// Per-hit DRUM lanes. Paired lanes are exact POST - PRE differences of event details measured
// over the same content samples (POST is measured at the PRE onset, B-1016); without a pair they
// are POST absolute observations. TRANSIENT is withheld, never estimated, when the next onset
// leaves no 20 ms body or when the body is below the HISTORY floor (it would describe the gap).
// A hit arrives with its head first (STRENGTH, CREST); TRANSIENT and SHARPNESS stay "--" until it
// is complete, and for good when its audio stopped first (B-1024).
namespace hypha::attack_lanes
{
enum class Lane : std::uint8_t
{
    transient,
    strength,
    crest,
    sharpness,
};

inline constexpr std::array<Lane, attack_ui::laneCount> lanes {
    Lane::transient, Lane::strength, Lane::crest, Lane::sharpness };

enum class Reason : std::uint8_t
{
    value,
    missing,       // detail or its body not delivered yet, or a non-finite descriptor
    noMatch,       // PRE-only, POST-only or ambiguous common event
    nextHit,       // TRANSIENT: the next onset leaves less than 20 ms of body
    quietBody,     // TRANSIENT: the body is below the HISTORY floor, silence or near-silence
};

struct Cell
{
    float value = std::numeric_limits<float>::quiet_NaN();
    Reason reason = Reason::missing;
};

struct Side
{
    bool available = false;
    bool complete = false; // false: only the head is measured
    std::int64_t onset = 0;
    std::array<float, attack_ui::laneCount> values {
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN() };
    bool bodyCut = false;
    float bodyDb = std::numeric_limits<float>::quiet_NaN();
};

struct Hit
{
    std::int64_t sample = 0;
    bool selectable = false; // B-778: only events with delivered POST detail can be selected.
    std::array<Cell, attack_ui::laneCount> cells {};
    Side pre {}, post {};
};

struct Model
{
    bool delta = false;
    std::uint32_t count = 0;
    std::array<Hit, KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY> hits {};
};

static_assert (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY >= KIRIN_ATTACK_DETAIL_BATCH_CAPACITY);

constexpr std::size_t index (Lane lane) noexcept
{
    return static_cast<std::size_t> (lane);
}

struct Scale
{
    float minimum = 0.0f;
    float maximum = 1.0f;
    bool fromZero = false; // bars grow from the value 0; otherwise from the scale minimum
};

// Fixed scales. The dB lanes share one range so the same change has the same bar length in every
// lane. Per-hit Sharpness differences stayed within about +/-0.4 acum and single hits within
// 0.2..6 acum in the B-1016 audit; head minus body ran from about -2 to +19 dB.
constexpr Scale scaleFor (Lane lane, bool delta) noexcept
{
    if (delta)
        return lane == Lane::sharpness ? Scale { -1.0f, 1.0f, true }
                                       : Scale { -12.0f, 12.0f, true };
    switch (lane)
    {
        case Lane::transient: return { -12.0f, 24.0f, true };
        case Lane::strength:  return { attack_ui::absoluteFloorDb, 0.0f, false };
        case Lane::crest:     return { 0.0f, 24.0f, false };
        case Lane::sharpness: return { 0.0f, 8.0f, false };
    }
    return {};
}

struct Extent
{
    float from = 0.0f; // 0 = bottom of the plot, 1 = top
    float to = 0.0f;
    bool clippedLow = false;
    bool clippedHigh = false;
};

constexpr Extent extentFor (float value, Scale scale) noexcept
{
    const auto span = scale.maximum - scale.minimum;
    if (! (span > 0.0f) || value != value)
        return {};
    const auto clamped = value < scale.minimum ? scale.minimum
                       : value > scale.maximum ? scale.maximum : value;
    const auto position = (clamped - scale.minimum) / span;
    const auto base = scale.fromZero ? (0.0f - scale.minimum) / span : 0.0f;
    return { base, position, value < scale.minimum, value > scale.maximum };
}

static_assert (extentFor (6.0f, scaleFor (Lane::transient, true)).to == 0.75f);
static_assert (extentFor (-30.0f, scaleFor (Lane::crest, true)).clippedLow);
static_assert (extentFor (-36.0f, scaleFor (Lane::strength, false)).to == 0.5f);
static_assert (extentFor (-6.0f, scaleFor (Lane::transient, false)).from * 3.0f == 1.0f);
static_assert (extentFor (4.0f, scaleFor (Lane::sharpness, false)).to == 0.5f);

inline const KirinAttackDetail* findDetail (const KirinAttackDetailBatch& batch,
                                            std::int64_t sample, std::uint64_t generation,
                                            std::uint32_t rate) noexcept
{
    const auto count = batch.count < KIRIN_ATTACK_DETAIL_BATCH_CAPACITY
        ? batch.count : static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY);
    for (std::uint32_t item = 0; item < count; ++item)
        if (batch.details[item].event_sample == sample
            && batch.details[item].generation == generation
            && batch.details[item].sample_rate == rate)
            return &batch.details[item];
    return nullptr;
}

inline Side sideFor (const KirinAttackDetail* detail) noexcept
{
    Side side;
    if (detail == nullptr)
        return side;
    side.available = true;
    side.complete = detail->complete != 0;
    side.onset = detail->event_sample;
    side.values[index (Lane::strength)] = detail->attack_rms_dbfs;
    side.values[index (Lane::crest)] = detail->crest_db;
    if (! side.complete)
        return side;
    side.bodyCut = detail->transient_available == 0;
    if (! side.bodyCut)
    {
        side.values[index (Lane::transient)] = detail->transient_db;
        side.bodyDb = detail->body_rms_dbfs;
    }
    if (detail->sharpness_available != 0)
        side.values[index (Lane::sharpness)] = detail->sharpness_acum;
    return side;
}

inline Cell withheld (Reason reason) noexcept
{
    return { std::numeric_limits<float>::quiet_NaN(), reason };
}

inline Cell measured (float value) noexcept
{
    return std::isfinite (value) ? Cell { value, Reason::value } : Cell {};
}

// Below -72 dBFS the body is under the HISTORY floor, so head minus body would mostly describe
// the quiet gap after the hit rather than its decay.
inline bool quietBody (const Side& side) noexcept
{
    return side.complete && ! side.bodyCut
        && (! std::isfinite (side.bodyDb) || side.bodyDb < attack_ui::absoluteFloorDb);
}

inline Cell transientCell (const Side& side) noexcept
{
    return ! side.complete ? withheld (Reason::missing)
         : side.bodyCut ? withheld (Reason::nextHit)
         : quietBody (side) ? withheld (Reason::quietBody)
         : measured (side.values[index (Lane::transient)]);
}

inline void fillDelta (Hit& hit, std::uint8_t kind, bool preOffered, bool postOffered) noexcept
{
    const auto withholdAll = [&hit] (Reason reason) { hit.cells.fill (withheld (reason)); };
    if (kind != 0 || ! preOffered || ! postOffered)
        return withholdAll (Reason::noMatch);
    // Matched POST is measured at the PRE onset; any other pairing of windows is not a difference.
    if (! hit.pre.available || ! hit.post.available || hit.pre.onset != hit.post.onset)
        return withholdAll (Reason::missing);
    for (const auto lane : lanes)
    {
        const auto pre = hit.pre.values[index (lane)];
        const auto post = hit.post.values[index (lane)];
        auto& cell = hit.cells[index (lane)];
        if (lane == Lane::transient && (! hit.pre.complete || ! hit.post.complete))
            cell = withheld (Reason::missing);
        else if (lane == Lane::transient && (hit.pre.bodyCut || hit.post.bodyCut))
            cell = withheld (Reason::nextHit);
        else if (lane == Lane::transient && (quietBody (hit.pre) || quietBody (hit.post)))
            cell = withheld (Reason::quietBody);
        else
            cell = std::isfinite (pre) && std::isfinite (post) ? measured (post - pre) : Cell {};
    }
}

inline void fillAbsolute (Hit& hit) noexcept
{
    for (const auto lane : lanes)
        hit.cells[index (lane)] = measured (hit.post.values[index (lane)]);
    hit.cells[index (Lane::transient)] = transientCell (hit.post);
}

// Snapshot inputs are already restricted to the current generation and sample rate.
inline void build (Model& model, const KirinAttackPairEventBatch& pairs,
                   const KirinAttackDetailBatch& post, const KirinAttackDetailBatch& pre,
                   std::uint64_t postGeneration, std::uint32_t rate) noexcept
{
    model.delta = pairs.status == KIRIN_SPECTRUM_ACTIVE;
    model.count = 0;
    if (model.delta)
    {
        const auto count = pairs.count < KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY
            ? pairs.count : static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY);
        for (std::uint32_t item = 0; item < count; ++item)
        {
            const auto& pair = pairs.events[item];
            auto& hit = model.hits[model.count++];
            hit = {};
            hit.sample = pair.event_sample;
            hit.post = sideFor (pair.post_available != 0 ? findDetail (
                post, pair.post_event_sample, pair.post_generation, pair.sample_rate) : nullptr);
            hit.pre = sideFor (pair.pre_available != 0 ? findDetail (
                pre, pair.pre_event_sample, pair.pre_generation, pair.sample_rate) : nullptr);
            hit.selectable = hit.post.available;
            fillDelta (hit, pair.kind, pair.pre_available != 0, pair.post_available != 0);
        }
        return;
    }
    const auto count = post.count < KIRIN_ATTACK_DETAIL_BATCH_CAPACITY
        ? post.count : static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY);
    for (std::uint32_t item = 0; item < count; ++item)
    {
        const auto& detail = post.details[item];
        if (detail.generation != postGeneration || detail.sample_rate != rate)
            continue;
        auto& hit = model.hits[model.count++];
        hit = {};
        hit.sample = detail.event_sample;
        hit.post = sideFor (&detail);
        hit.selectable = true;
        fillAbsolute (hit);
    }
}

inline const Hit* find (const Model& model, std::int64_t sample) noexcept
{
    for (std::uint32_t item = 0; item < model.count; ++item)
        if (model.hits[item].sample == sample)
            return &model.hits[item];
    return nullptr;
}

// The hits on the six-second axis: exactly the columns the lanes draw.
inline std::uint32_t visibleCount (const Model& model, std::int64_t latest,
                                   std::uint32_t rate) noexcept
{
    std::uint32_t visible = 0;
    for (std::uint32_t item = 0; item < model.count; ++item)
        visible += attack_ui::eventIsVisible (model.hits[item].sample, latest, rate) ? 1u : 0u;
    return visible;
}
}
