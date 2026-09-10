#pragma once

#include "kirin_hypha_ffi.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <tuple>
#include <type_traits>

namespace hypha::observation_equality
{
// Compare facts, not ABI padding. Repeated unavailable NaNs are unchanged; integer clocks
// retain all 64 bits. Any ABI extension must explicitly extend these semantic comparisons.
static_assert (sizeof (KirinMeasureResult) == 416 && sizeof (KirinWatchDisplay) == 832);
static_assert (sizeof (KirinMeterSession) == 872 && sizeof (KirinDelta) == 224);
static_assert (sizeof (KirinObservatoryFrame) == 1112 && sizeof (KirinMeterHistoryEntry) == 184);

template <typename T> bool field (const T& a, const T& b) noexcept
{
    if constexpr (std::is_floating_point_v<T>)
        return std::equal_to<T> {} (a, b) || (std::isnan (a) && std::isnan (b));
    else if constexpr (std::is_array_v<T>)
        return std::equal (std::begin (a), std::end (a), std::begin (b),
                           [] (const auto& x, const auto& y) { return field (x, y); });
    else
        return a == b;
}

template <typename Tuple, size_t... I>
bool fields (const Tuple& a, const Tuple& b, std::index_sequence<I...>) noexcept
{
    return (field (std::get<I> (a), std::get<I> (b)) && ...);
}

template <typename Tuple> bool fields (const Tuple& a, const Tuple& b) noexcept
{
    return fields (a, b, std::make_index_sequence<std::tuple_size_v<Tuple>> {});
}

inline auto key (const KirinMeasureResult& v) noexcept
{
    return std::tie (v.lufs_m, v.true_peak, v.crest, v.psr, v.n_prime_total, v.sharpness,
        v.psb_low, v.psb_mid, v.psb_high, v.n_prime, v.psb_bark, v.tp_session_max,
        v.dropped_samples, v.lufs_s);
}
inline auto key (const KirinDelta& v) noexcept
{
    return std::tie (v.mode, v.lufs, v.true_peak, v.crest, v.psr, v.n_prime_total,
                     v.sharpness, v.lufs_s, v.psb_bark);
}
inline auto key (const KirinMeterSession& v) noexcept
{
    return std::tie (v.generation, v.active_frames, v.observed_frames, v.sample_rate, v.state,
        v.lufs_m, v.lufs_s, v.lufs_i, v.lra, v.true_peak, v.max_true_peak, v.plr,
        v.channels, v.balance_state, v.sample_peak_dbfs, v.sample_peak_hold_dbfs,
        v.channel_true_peak_dbtp, v.channel_max_true_peak_dbtp,
        v.channel_clip_latched, v.clip_events,
        v.balance_db, v.correlation, v.field_size, v.field_observation_count,
        v.field_density, v.max_lufs_m, v.channel_vu_dbfs,
        v.channel_instant_true_peak_dbtp);
}
inline auto key (const KirinMeterHistoryRange& v) noexcept
{
    return std::tie (v.min, v.max, v.mean);
}
template <typename T> bool same (const T& a, const T& b) noexcept
{
    return fields (key (a), key (b));
}
inline bool same (const KirinWatchDisplay& a, const KirinWatchDisplay& b) noexcept
{
    return same (a.current, b.current) && same (a.maximum, b.maximum);
}
inline bool same (const KirinObservatoryFrame& a, const KirinObservatoryFrame& b) noexcept
{
    return fields (std::tie (a.version, a.signal_state, a.lra_state, a.delta_available,
                            a.lra_elapsed_seconds),
                   std::tie (b.version, b.signal_state, b.lra_state, b.delta_available,
                            b.lra_elapsed_seconds))
        && same (a.meter, b.meter) && same (a.delta, b.delta);
}
inline bool same (const KirinMeterHistoryEntry& a, const KirinMeterHistoryEntry& b) noexcept
{
    return fields (std::tie (a.generation, a.run_id, a.first_observed_frames, a.last_observed_frames,
                            a.first_timeline_endpoint_samples, a.last_timeline_endpoint_samples,
                            a.observation_count, a.resolution, a.clip_event_count),
                   std::tie (b.generation, b.run_id, b.first_observed_frames, b.last_observed_frames,
                            b.first_timeline_endpoint_samples, b.last_timeline_endpoint_samples,
                            b.observation_count, b.resolution, b.clip_event_count))
        && same (a.lufs_m, b.lufs_m) && same (a.lufs_s, b.lufs_s)
        && same (a.true_peak, b.true_peak) && same (a.correlation, b.correlation)
        && same (a.plr, b.plr);
}
}
