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
// With a band chosen (B-1097) the same four rows carry the band lanes DELAY, ATT, REL and LEVEL,
// built by attack_band::build (HyphaAttackBandModel.h) into the same Model.
namespace hypha::attack_lanes
{
enum class Lane : std::uint8_t
{
    transient,
    strength,
    crest,
    sharpness,
    delay,      // band: POST arrival - PRE arrival, ms
    attackTime, // band: ATT, 10 % to 90 % of the band peak, ms
    release,    // band: REL, band peak to peak - 20 dB, ms
    level,      // band: the band's peak envelope level
};

inline constexpr std::array<Lane, attack_ui::laneCount> lanes {
    Lane::transient, Lane::strength, Lane::crest, Lane::sharpness };
inline constexpr std::array<Lane, attack_ui::laneCount> bandLanes {
    Lane::delay, Lane::attackTime, Lane::release, Lane::level };

enum class Reason : std::uint8_t
{
    value,
    missing,          // not delivered or not measured yet ("--")
    noMatch,          // PRE-only, POST-only or ambiguous common event
    nextHit,          // TRANSIENT: the next onset leaves less than 20 ms of body; REL: tail cut
    quietBody,        // TRANSIENT: the body is below the HISTORY floor, silence or near-silence
    // The band lanes (B-1098). Each is a fact the engine stated, never inferred here.
    withinResolution, // ATT (or its change) inside one period of the band's centre: `value` is
                      // that period, shown as an upper bound "<16 ms"
    atLeast,          // a bound: REL past the measured tail, or LEVEL against a silent side;
                      // `value` is the bound, positive "at least", negative "at most"
    ringing,          // the previous hit still rings in the band
    noSound,          // the band is below -72 dBFS at this hit
    preNoSound,       // paired: PRE's band is silent at this hit
    postNoSound,      // paired: POST's band is silent at this hit
    longTail,         // REL: both sides ring past the measured tail
    notMeasured,      // the hit's audio was not kept: before the band was chosen, or stopped
    updatePre,        // DELAY: the paired PRE predates bands
    noPair,           // DELAY without a PRE
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
    float resolutionMs = 0.0f; // band: one period of the centre; 0 for the whole signal
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

constexpr bool isBand (Lane lane) noexcept
{
    return static_cast<std::size_t> (lane) >= attack_ui::laneCount;
}

// The row a lane occupies, and its cell in a hit: the whole-signal and band lanes share rows.
constexpr std::size_t index (Lane lane) noexcept
{
    return static_cast<std::size_t> (lane) % attack_ui::laneCount;
}

struct Scale
{
    float minimum = 0.0f;
    float maximum = 1.0f;
    bool fromZero = false; // bars grow from the value 0; otherwise from the scale minimum
};

// Fixed scales. The dB lanes share one range so the same change has the same bar length in every
// lane. Per-hit Sharpness differences stayed within about +/-0.4 acum and single hits within
// 0.2..6 acum in the B-1016 audit; head minus body ran from about -2 to +19 dB. The band time
// scales are the plan's first values (DELAY and ATT +/-10 ms, REL +/-100 ms), to be reviewed
// against measured drums.
constexpr Scale scaleFor (Lane lane, bool delta) noexcept
{
    if (delta)
        switch (lane)
        {
            case Lane::transient:
            case Lane::strength:
            case Lane::crest:
            case Lane::level:      return { -12.0f, 12.0f, true };
            case Lane::sharpness:  return { -1.0f, 1.0f, true };
            case Lane::delay:
            case Lane::attackTime: return { -10.0f, 10.0f, true };
            case Lane::release:    return { -100.0f, 100.0f, true };
        }
    switch (lane)
    {
        case Lane::transient:  return { -12.0f, 24.0f, true };
        case Lane::strength:   return { attack_ui::absoluteFloorDb, 0.0f, false };
        case Lane::crest:      return { 0.0f, 24.0f, false };
        case Lane::sharpness:  return { 0.0f, 8.0f, false };
        case Lane::delay:      return { -10.0f, 10.0f, true }; // never a value without PRE
        case Lane::attackTime: return { 0.0f, 40.0f, false };
        case Lane::release:    return { 0.0f, 300.0f, false };
        case Lane::level:      return { attack_ui::absoluteFloorDb, 0.0f, false };
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
static_assert (extentFor (5.0f, scaleFor (Lane::delay, true)).to == 0.75f);
static_assert (extentFor (-50.0f, scaleFor (Lane::release, true)).to == 0.25f);
static_assert (extentFor (150.0f, scaleFor (Lane::release, false)).to == 0.5f);
static_assert (index (Lane::level) == index (Lane::sharpness) && isBand (Lane::level));

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
