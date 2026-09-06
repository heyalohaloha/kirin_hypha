#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <tuple>
#include <type_traits>
#include "kirin_hypha_ffi.h"

namespace hypha::attack_equality
{
static_assert (sizeof (KirinAttackEvent) == 72 && sizeof (KirinAttackDetail) == 512);
static_assert (sizeof (KirinAttackWaveformPoint) == 40 && sizeof (KirinAttackPairEvent) == 112);
static_assert (sizeof (KirinAttackStats) == 32);
template <typename T> bool field (const T& a, const T& b) noexcept
{
    if constexpr (std::is_floating_point_v<T>)
        return std::equal_to<T> {} (a, b) || (std::isnan (a) && std::isnan (b));
    else if constexpr (std::is_array_v<T>)
        return std::equal (std::begin (a), std::end (a), std::begin (b),
                           [] (const auto& x, const auto& y) { return field (x, y); });
    else return a == b;
}
template <typename T, std::size_t... I>
bool fields (const T& a, const T& b, std::index_sequence<I...>) noexcept
{ return (field (std::get<I> (a), std::get<I> (b)) && ...); }
inline auto key (const KirinAttackEvent& v) noexcept
{ return std::tie (v.generation, v.sample_rate, v.channels, v.definition_hash,
                   v.event_sample, v.decision_sample, v.value); }
inline auto key (const KirinAttackWaveformPoint& v) noexcept
{ return std::tie (v.generation, v.sample_rate, v.channels, v.start_sample,
                   v.end_sample, v.peak_linear, v.rms_dbfs); }
inline auto key (const KirinAttackDetail& v) noexcept
{
    return std::tie (v.generation, v.sample_rate, v.channels, v.temporal_centroid_available,
        v.sharpness_available, v.definition_hash, v.event_sample, v.decision_sample,
        v.shape_start_sample, v.shape_end_sample, v.value, v.contrast_db, v.context_rms_dbfs,
        v.attack_rms_dbfs, v.sample_peak_dbfs, v.crest_db, v.sample_edge_ratio_db,
        v.peak_plateau_ms, v.temporal_centroid_ms, v.sharpness_acum, v.shape_count, v.shape);
}
inline auto key (const KirinAttackPairEvent& v) noexcept
{
    return std::tie (v.pair_generation, v.pre_generation, v.post_generation, v.sample_rate,
        v.channels, v.kind, v.pre_available, v.post_available, v.definition_hash,
        v.event_sample, v.decision_sample, v.pre_event_sample, v.post_event_sample,
        v.pre_value, v.post_value, v.delta_value, v.delta_available);
}
inline auto key (const KirinAttackStats& v) noexcept
{ return std::tie (v.available, v.enabled, v.worker_running, v.channels,
                   v.pushed_blocks, v.dropped_blocks, v.analyzed_frames); }
template <typename T> bool same (const T& a, const T& b) noexcept
{
    const auto ka = key (a), kb = key (b);
    return fields (ka, kb, std::make_index_sequence<std::tuple_size_v<decltype (ka)>> {});
}
// Compare every retained item, including interior corrections and late PRE arrival. Padding,
// unused capacity and stale-generation entries cannot manufacture a new visual observation.
template <typename T, std::size_t N, typename Valid>
bool retained (const T (&stored)[N], std::uint32_t storedCount,
               const T (&incoming)[N], std::uint32_t incomingCount, Valid valid) noexcept
{
    std::size_t out = 0;
    for (std::size_t i = 0; i < std::min<std::size_t> (incomingCount, N); ++i)
        if (valid (incoming[i]))
        {
            if (out >= std::min<std::size_t> (storedCount, N)
                || ! same (stored[out++], incoming[i])) return false;
        }
    return out == std::min<std::size_t> (storedCount, N);
}
}
