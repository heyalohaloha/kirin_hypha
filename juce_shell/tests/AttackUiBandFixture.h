#pragma once

#include "AttackUiLaneContract.h"
#include "../src/HyphaAttackBandContract.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

// Band fixtures for the DRUM contracts: the engine's records for exactly the lanes' hits, keyed
// as the engine keys them, and the envelopes behind them.
namespace hypha::attack_ui_test
{
// A side that rises: a linear rise to the peak, then an exponential ring-out. The engine's times:
// arrival at 10 % of the peak amplitude, ATT from 10 % to 90 %, REL from the peak to -20 dB.
inline KirinAttackBandSide risingSide (float delayMs, float riseMs, float tauMs, float peakDb)
{
    KirinAttackBandSide side {};
    side.state = KIRIN_ATTACK_BAND_SIDE_RISES;
    side.arrival_state = KIRIN_ATTACK_BAND_ARRIVAL_AT;
    side.arrival_ms = delayMs + riseMs * 0.1f;
    side.attack_ms = riseMs * 0.8f;
    side.peak_ms = delayMs + riseMs;
    side.release_ms = tauMs * std::log (10.0f);
    side.release_state = KIRIN_ATTACK_BAND_RELEASE_AT;
    if (side.peak_ms + side.release_ms > 300.0f)
    {
        side.release_state = KIRIN_ATTACK_BAND_RELEASE_AT_LEAST;
        side.release_ms = 300.0f - side.peak_ms;
    }
    side.level_dbfs = peakDb;
    return side;
}

inline KirinAttackBandSide sideIn (std::uint8_t state)
{
    KirinAttackBandSide side {};
    side.state = state;
    return side;
}

// The envelope behind a side, from the same rise and ring-out.
inline KirinAttackBandEnvelope envelopeFor (const KirinAttackBandSide& side)
{
    KirinAttackBandEnvelope envelope {};
    const auto rise = side.attack_ms / 0.8f;
    const auto delay = side.arrival_ms - rise * 0.1f;
    const auto tau = side.release_state == KIRIN_ATTACK_BAND_RELEASE_AT
        ? side.release_ms / std::log (10.0f) : 200.0f;
    const auto level = [&] (float ms) {
        const auto t = ms - delay;
        if (side.state != KIRIN_ATTACK_BAND_SIDE_RISES || t <= 0.0f)
            return -120.0f;
        const auto amplitude = std::min (1.0f, t / rise) * (t > rise ? std::exp (-(t - rise) / tau) : 1.0f);
        return std::max (-120.0f, side.level_dbfs + 20.0f * std::log10 (std::max (1.0e-6f, amplitude)));
    };
    for (std::size_t point = 0; point < KIRIN_ATTACK_BAND_HEAD_POINTS; ++point)
        envelope.head_dbfs[point] = level (attack_band::headPointsFromMs
            + (static_cast<float> (point) + 0.5f) * 60.0f / KIRIN_ATTACK_BAND_HEAD_POINTS);
    for (std::size_t point = 0; point < KIRIN_ATTACK_BAND_TAIL_POINTS; ++point)
        envelope.tail_dbfs[point] = level ((static_cast<float> (point) + 0.5f) * 300.0f
                                           / KIRIN_ATTACK_BAND_TAIL_POINTS);
    return envelope;
}

// Every lanes hit of `fixture` in `band`, keyed by the lanes' own key: the pair event while a pair
// is active, POST's detail otherwise. PRE 6 ms rise, 45 ms ring-out, -6 dBFS; POST 2.4 ms later,
// 7 ms, 55 ms, -6.8 dBFS (the approved mock's numbers).
inline std::unique_ptr<KirinAttackBandBatch> bandBatchFor (const LaneFixture& fixture,
                                                         std::uint8_t band,
                                                         std::uint8_t preBand = KIRIN_ATTACK_BAND_PRE_SAME)
{
    auto batch = std::make_unique<KirinAttackBandBatch>();
    const bool paired = fixture.pairs->status == KIRIN_SPECTRUM_ACTIVE;
    batch->status = fixture.pairs->status;
    batch->band = band;
    batch->pre_band = paired ? preBand : KIRIN_ATTACK_BAND_PRE_OFF;
    batch->capacity = KIRIN_ATTACK_BAND_BATCH_CAPACITY;
    batch->resolution_micros = band != 0
        ? static_cast<std::uint32_t> (std::lround (attack_band::bandFor (band)->periodMs() * 1'000.0f)) : 0;
    batch->generation = 7;
    batch->sample_rate = 48'000;
    const auto pre = risingSide (0.0f, 6.0f, 45.0f, -6.0f);
    const auto post = risingSide (2.4f, 7.0f, 55.0f, -6.8f);
    if (paired)
        for (std::uint32_t item = 0; item < fixture.pairs->count; ++item)
        {
            const auto& pair = fixture.pairs->events[item];
            auto& hit = batch->hits[batch->count++];
            hit.event_sample = pair.event_sample;
            hit.measured_at_sample = pair.pre_event_sample;
            hit.kind = pair.kind;
            hit.pre = pair.kind == 0 || pair.kind == 1 ? pre : sideIn (KIRIN_ATTACK_BAND_SIDE_ABSENT);
            hit.post = pair.kind == 0 || pair.kind == 2 ? post : sideIn (KIRIN_ATTACK_BAND_SIDE_ABSENT);
        }
    else
        for (std::uint32_t item = 0; item < fixture.post->count; ++item)
        {
            auto& hit = batch->hits[batch->count++];
            hit.event_sample = hit.measured_at_sample = fixture.post->details[item].event_sample;
            hit.kind = KIRIN_ATTACK_BAND_KIND_POST_ALONE;
            hit.pre = sideIn (KIRIN_ATTACK_BAND_SIDE_ABSENT);
            hit.post = post;
        }
    return batch;
}

// Hits that differ as a real kick's do, around bandBatchFor's numbers: POST's delay, ring-out and
// level vary by hit; with `leftOut`, one hit of every six rings on in the band and is left out.
inline void varyHits (KirinAttackBandBatch& batch, bool leftOut)
{
    constexpr float delay[8] { -0.5f, 0.3f, 0.0f, 0.6f, -0.2f, 0.1f, -0.4f, 0.8f };
    constexpr float tau[8] { -4.0f, 6.0f, 0.0f, 3.0f, -2.0f, 8.0f, -5.0f, 1.0f };
    constexpr float level[8] { 0.2f, -0.3f, 0.0f, 0.4f, -0.1f, 0.3f, -0.2f, 0.1f };
    for (std::uint32_t item = 0; item < batch.count; ++item)
    {
        auto& hit = batch.hits[item];
        if (hit.post.state != KIRIN_ATTACK_BAND_SIDE_RISES)
            continue;
        if (leftOut && item % 6 == 4)
        {
            hit.post = sideIn (KIRIN_ATTACK_BAND_SIDE_RINGS_ON);
            continue;
        }
        const auto index = item % 8;
        hit.post = risingSide (2.4f + delay[index], 7.0f, 55.0f + tau[index], -6.8f + level[index]);
    }
}

// What the engine's envelope poll answers for `batch`, and the samples it was asked for.
struct EnvelopeSource
{
    const KirinAttackBandBatch* batch = nullptr;
    std::shared_ptr<std::vector<std::int64_t>> asked = std::make_shared<std::vector<std::int64_t>>();

    bool operator() (std::int64_t sample, KirinAttackBandHitEnvelope& out) const
    {
        asked->push_back (sample);
        for (std::uint32_t item = 0; batch != nullptr && item < batch->count; ++item)
            if (batch->hits[item].event_sample == sample)
            {
                out = {};
                out.hit = batch->hits[item];
                out.band = batch->band;
                out.pre = envelopeFor (out.hit.pre);
                out.post = envelopeFor (out.hit.post);
                return true;
            }
        return false;
    }
};

// The engine's summary of `batch`, by the engine's rules (attack_ffi_band_summary.rs): the newest
// eight hits whose band rises on every compared side, each lane's median, spread and agreement,
// the median marks and the average envelopes.
inline KirinAttackBandSummary summaryFor (const KirinAttackBandBatch& batch)
{
    KirinAttackBandSummary summary {};
    summary.status = batch.status;
    summary.band = batch.band;
    summary.pre_band = batch.pre_band;
    summary.resolution_micros = batch.resolution_micros;
    summary.generation = batch.generation;
    summary.sample_rate = batch.sample_rate;
    const bool delta = batch.status == KIRIN_SPECTRUM_ACTIVE
                    && (batch.pre_band == KIRIN_ATTACK_BAND_PRE_SAME || batch.pre_band == KIRIN_ATTACK_BAND_PRE_WAITING);
    summary.delta = delta ? 1 : 0;
    const auto rises = [] (const KirinAttackBandSide& side) { return side.state == KIRIN_ATTACK_BAND_SIDE_RISES; };
    const auto decided = [] (const KirinAttackBandSide& side) {
        return side.state != KIRIN_ATTACK_BAND_SIDE_PENDING && side.state != KIRIN_ATTACK_BAND_SIDE_ABSENT;
    };
    std::vector<const KirinAttackBandHit*> summed;
    std::uint32_t leftOut = 0;
    for (auto index = static_cast<int> (batch.count) - 1; index >= 0 && summed.size() < 8; --index)
    {
        const auto& hit = batch.hits[index];
        if (delta ? hit.kind != 0 || ! decided (hit.pre) || ! decided (hit.post) : ! decided (hit.post))
            continue;
        if (delta ? rises (hit.pre) && rises (hit.post) : rises (hit.post))
            summed.insert (summed.begin(), &hit);
        else
            ++leftOut;
    }
    summary.count = static_cast<std::uint32_t> (summed.size());
    summary.left_out = summed.empty() ? 0 : leftOut;
    // Why hits have no start or fall to compare: the most common reason among them.
    std::array<std::array<int, 4>, 2> held {};
    for (const auto* hit : summed)
    {
        const auto arrives = [] (const KirinAttackBandSide& side) { return side.arrival_state == KIRIN_ATTACK_BAND_ARRIVAL_AT; };
        const auto state = [] (const KirinAttackBandSide& side) { return side.release_state; };
        if (delta ? ! arrives (hit->pre) || ! arrives (hit->post) : ! arrives (hit->post))
            ++held[0][KIRIN_ATTACK_BAND_HELD_RINGING];
        const bool cut = state (hit->post) == KIRIN_ATTACK_BAND_RELEASE_NEXT_HIT
                      || (delta && state (hit->pre) == KIRIN_ATTACK_BAND_RELEASE_NEXT_HIT);
        const bool past = state (hit->post) != KIRIN_ATTACK_BAND_RELEASE_AT
                       || (delta && state (hit->pre) != KIRIN_ATTACK_BAND_RELEASE_AT);
        if (cut || past)
            ++held[1][cut ? KIRIN_ATTACK_BAND_HELD_NEXT_HIT : KIRIN_ATTACK_BAND_HELD_LONG_TAIL];
    }
    const auto mostCommon = [] (const std::array<int, 4>& counts) {
        std::uint8_t best = KIRIN_ATTACK_BAND_HELD_NONE;
        for (std::uint8_t reason = 1; reason < 4; ++reason)
            if (counts[reason] > counts[best] || (best == KIRIN_ATTACK_BAND_HELD_NONE && counts[reason] > 0))
                best = reason;
        return best;
    };
    summary.lanes[0].withheld = delta ? mostCommon (held[0]) : KIRIN_ATTACK_BAND_HELD_NONE;
    summary.lanes[1].withheld = mostCommon (held[0]);
    summary.lanes[2].withheld = mostCommon (held[1]);
    const auto resolution = static_cast<float> (batch.resolution_micros) / 1'000.0f;
    const std::array<float, 4> within { std::max (0.2f, resolution / 32.0f), resolution, resolution, 0.2f };
    for (std::size_t lane = 0; lane < 4; ++lane)
    {
        auto& entry = summary.lanes[lane];
        std::vector<float> present;
        for (std::size_t index = 0; index < 8; ++index)
            entry.values[index] = std::numeric_limits<float>::quiet_NaN();
        for (std::size_t index = 0; index < summed.size(); ++index)
        {
            const auto& pre = summed[index]->pre;
            const auto& post = summed[index]->post;
            constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
            const auto released = [] (const KirinAttackBandSide& side) {
                return side.release_state == KIRIN_ATTACK_BAND_RELEASE_AT;
            };
            const auto arrived = [] (const KirinAttackBandSide& side) {
                return side.arrival_state == KIRIN_ATTACK_BAND_ARRIVAL_AT;
            };
            const bool both = arrived (pre) && arrived (post);
            const float values[4] { both ? post.arrival_ms - pre.arrival_ms : nan,
                                    both ? post.attack_ms - pre.attack_ms : nan,
                                    released (pre) && released (post) ? post.release_ms - pre.release_ms : nan,
                                    post.level_dbfs - pre.level_dbfs };
            const float own[4] { nan, arrived (post) ? post.attack_ms : nan, released (post) ? post.release_ms : nan,
                                 post.level_dbfs };
            entry.values[index] = delta ? values[lane] : own[lane];
            if (std::isfinite (entry.values[index]))
                present.push_back (entry.values[index]);
        }
        entry.within = within[lane];
        if (present.empty())
            continue;
        std::sort (present.begin(), present.end());
        const auto middle = present.size() / 2;
        entry.median = present.size() % 2 == 1 ? present[middle] : 0.5f * (present[middle - 1] + present[middle]);
        entry.low = present.front();
        entry.high = present.back();
        entry.count = static_cast<std::uint8_t> (present.size());
        const bool inside = delta ? std::abs (entry.median) <= entry.within : lane == 1 && entry.median < entry.within;
        entry.state = inside ? KIRIN_ATTACK_BAND_LANE_WITHIN : KIRIN_ATTACK_BAND_LANE_VALUE;
        if (delta && ! inside)
            entry.agree = static_cast<std::uint8_t> (std::count_if (present.begin(), present.end(),
                [&entry] (float value) { return value != 0.0f && (value > 0.0f) == (entry.median > 0.0f); }));
    }
    for (std::size_t index = 0; index < summed.size(); ++index)
        summary.event_samples[index] = summed[index]->event_sample;
    // What the engine leaves without hits (and without PRE): no marks, no envelopes.
    constexpr auto none = std::numeric_limits<float>::quiet_NaN();
    summary.pre_arrival_ms = summary.post_arrival_ms = none;
    summary.pre_release_end_ms = summary.post_release_end_ms = none;
    for (auto* envelope : { &summary.pre, &summary.post, &summary.post_low, &summary.post_high })
    {
        std::fill (std::begin (envelope->head_dbfs), std::end (envelope->head_dbfs), none);
        std::fill (std::begin (envelope->tail_dbfs), std::end (envelope->tail_dbfs), none);
    }
    if (summed.empty())
        return summary;
    const auto medianOf = [] (std::vector<float> values) {
        std::sort (values.begin(), values.end());
        const auto middle = values.size() / 2;
        return values.size() % 2 == 1 ? values[middle] : 0.5f * (values[middle - 1] + values[middle]);
    };
    std::vector<float> preArrival, postArrival, preEnd, postEnd;
    for (const auto* hit : summed)
    {
        if (hit->pre.arrival_state == KIRIN_ATTACK_BAND_ARRIVAL_AT)
            preArrival.push_back (hit->pre.arrival_ms);
        if (hit->post.arrival_state == KIRIN_ATTACK_BAND_ARRIVAL_AT)
            postArrival.push_back (hit->post.arrival_ms);
        if (hit->pre.release_state == KIRIN_ATTACK_BAND_RELEASE_AT)
            preEnd.push_back (hit->pre.peak_ms + hit->pre.release_ms);
        if (hit->post.release_state == KIRIN_ATTACK_BAND_RELEASE_AT)
            postEnd.push_back (hit->post.peak_ms + hit->post.release_ms);
    }
    summary.post_arrival_ms = postArrival.empty() ? none : medianOf (postArrival);
    summary.post_release_end_ms = postEnd.empty() ? none : medianOf (postEnd);
    const auto count = static_cast<float> (summed.size());
    const auto fold = [&summed, count] (bool pre, int pick) {
        KirinAttackBandEnvelope result {};
        const auto each = [pick, count] (float& into, float value, bool firstHit) {
            into = firstHit ? (pick == 0 ? value / count : value)
                            : pick == 0 ? into + value / count : pick < 0 ? std::min (into, value) : std::max (into, value);
        };
        for (std::size_t index = 0; index < summed.size(); ++index)
        {
            const auto envelope = envelopeFor (pre ? summed[index]->pre : summed[index]->post);
            for (std::size_t point = 0; point < KIRIN_ATTACK_BAND_HEAD_POINTS; ++point)
                each (result.head_dbfs[point], envelope.head_dbfs[point], index == 0);
            for (std::size_t point = 0; point < KIRIN_ATTACK_BAND_TAIL_POINTS; ++point)
                each (result.tail_dbfs[point], envelope.tail_dbfs[point], index == 0);
        }
        return result;
    };
    if (delta)
    {
        summary.pre_arrival_ms = preArrival.empty() ? none : medianOf (preArrival);
        summary.pre_release_end_ms = preEnd.empty() ? none : medianOf (preEnd);
        summary.pre = fold (true, 0);
    }
    summary.post = fold (false, 0);
    summary.post_low = fold (false, -1);
    summary.post_high = fold (false, 1);
    return summary;
}

// Pair events whose common, PRE and POST onsets differ as a real chain's do: the common onset
// 3 ms after PRE's, POST measured at PRE's onset (as the engine reports a matched pair).
inline void shiftCommonOnsets (LaneFixture& fixture)
{
    for (std::uint32_t item = 0; item < fixture.pairs->count; ++item)
        fixture.pairs->events[item].event_sample += 144;
}
}
