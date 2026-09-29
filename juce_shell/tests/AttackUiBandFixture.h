#pragma once

#include "AttackUiLaneContract.h"
#include "../src/HyphaAttackBandContract.h"

#include <cmath>
#include <functional>
#include <memory>

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

// Pair events whose common, PRE and POST onsets differ as a real chain's do: the common onset
// 3 ms after PRE's, POST measured at PRE's onset (as the engine reports a matched pair).
inline void shiftCommonOnsets (LaneFixture& fixture)
{
    for (std::uint32_t item = 0; item < fixture.pairs->count; ++item)
        fixture.pairs->events[item].event_sample += 144;
}
}
