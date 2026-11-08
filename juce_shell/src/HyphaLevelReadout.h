#pragma once
#include "HyphaDisplaySettings.h"
#include "kirin_hypha_ffi.h"
#include <cmath>

namespace hypha::level_readout
{
template <typename Value>
void availability (Value& held, Value next) noexcept
{
    if (std::isfinite (held) != std::isfinite (next)) held = next;
}
template <typename Value, size_t Count>
void availability (Value (&held)[Count], const Value (&next)[Count]) noexcept
{
    for (size_t i = 0; i < Count; ++i) availability (held[i], next[i]);
}
inline void availability (KirinMeterSession& held, const KirinMeterSession& next) noexcept
{
    for (auto member : { &KirinMeterSession::lufs_m, &KirinMeterSession::lufs_s,
            &KirinMeterSession::lufs_i, &KirinMeterSession::lra, &KirinMeterSession::true_peak,
            &KirinMeterSession::max_true_peak, &KirinMeterSession::plr, &KirinMeterSession::max_lufs_m,
            &KirinMeterSession::balance_db, &KirinMeterSession::correlation })
        availability (held.*member, next.*member);
    for (auto member : { &KirinMeterSession::sample_peak_dbfs, &KirinMeterSession::sample_peak_hold_dbfs,
            &KirinMeterSession::channel_true_peak_dbtp, &KirinMeterSession::channel_max_true_peak_dbtp,
            &KirinMeterSession::channel_vu_dbfs, &KirinMeterSession::channel_instant_true_peak_dbtp })
        availability (held.*member, next.*member);
    availability (held.mono_sum_db, next.mono_sum_db);
}
inline void availability (KirinMeasureResult& held, const KirinMeasureResult& next) noexcept
{
    for (auto member : { &KirinMeasureResult::lufs_m, &KirinMeasureResult::lufs_s,
            &KirinMeasureResult::true_peak, &KirinMeasureResult::tp_session_max,
            &KirinMeasureResult::crest, &KirinMeasureResult::psr, &KirinMeasureResult::n_prime_total,
            &KirinMeasureResult::sharpness, &KirinMeasureResult::psb_low,
            &KirinMeasureResult::psb_mid, &KirinMeasureResult::psb_high })
        availability (held.*member, next.*member);
    availability (held.n_prime, next.n_prime);
    availability (held.psb_bark, next.psb_bark);
}
// A held reading, not an average. Availability is always read from the current publication.
// Session, pair, state and layout boundaries replace the numbers immediately.
class Display
{
public:
    void setInterval (double ms) noexcept { if (std::abs (interval - ms) > 0.0) { interval = ms; ready = false; } }
    const KirinObservatoryFrame& read (const KirinObservatoryFrame& next, double now)
    {
        const auto& a = held.meter; const auto& b = next.meter;
        const bool boundary = a.generation != b.generation || a.measurement_epoch != b.measurement_epoch
            || a.sample_rate != b.sample_rate || a.layout_id != b.layout_id || a.channels != b.channels
            || a.state != b.state || held.signal_state != next.signal_state
            || held.comparison_identity != next.comparison_identity
            || held.comparison_generation != next.comparison_generation
            || held.lra_state != next.lra_state
            || held.meter_stalled != next.meter_stalled
            || held.delta_available != next.delta_available || held.delta.mode != next.delta.mode;
        if (! ready || boundary || interval <= 0 || now < updated || now - updated >= interval)
        { held = next; updated = now; ready = true; ++revision; }
        else
        {
            availability (held.meter, next.meter);
            for (auto member : { &KirinDelta::lufs, &KirinDelta::lufs_s, &KirinDelta::true_peak,
                    &KirinDelta::crest, &KirinDelta::psr, &KirinDelta::n_prime_total, &KirinDelta::sharpness })
                availability (held.delta.*member, next.delta.*member);
            availability (held.delta.psb_bark, next.delta.psb_bark);
        }
        return held;
    }
    const KirinWatchDisplay& watch (const KirinWatchDisplay& next, bool available)
    {
        if (! available) { watchReady = false; return next; }
        if (! watchReady || watchRevision != revision) { heldWatch = next; watchRevision = revision; }
        else { availability (heldWatch.current, next.current); availability (heldWatch.maximum, next.maximum); }
        watchReady = true;
        return heldWatch;
    }
    const KirinMeterSession& summary (const KirinMeterSession& next, int coverage = -1)
    {
        if (summaryRevision != revision || summaryCoverage != coverage)
        { heldSummary = next; summaryRevision = revision; summaryCoverage = coverage; }
        else availability (heldSummary, next);
        return heldSummary;
    }
    const KirinObservatoryFrame& frame() const noexcept { return held; }
    double updated = 0;
private:
    KirinObservatoryFrame held {};
    double interval = 0;
    bool ready = false, watchReady = false;
    uint64_t revision = 0, watchRevision = UINT64_MAX, summaryRevision = UINT64_MAX;
    KirinWatchDisplay heldWatch {};
    KirinMeterSession heldSummary {};
    int summaryCoverage = -1;
};
}
