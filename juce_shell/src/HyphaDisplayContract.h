#pragma once

#include <cstdint>

#include "../../crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h"

// Pure C++ POST presentation decisions shared by the AU/VST3 editor. Pair ownership remains in
// Rust; this contract only decides whether the six common metric cells show POST absolute values
// or POST-PRE deltas for the producer's ABI mode.
namespace hypha::display_contract
{
    enum class MetricMode
    {
        absolute,
        delta,
    };

    constexpr bool deltaIsActive (std::uint8_t mode) noexcept
    {
        return mode == KIRIN_DELTA_MODE_ACTIVE;
    }

    constexpr bool deltaIsStale (std::uint8_t mode) noexcept
    {
        return mode == KIRIN_DELTA_MODE_STALE;
    }

    constexpr bool preUnavailableForDelta (std::uint8_t mode) noexcept
    {
        return mode == KIRIN_DELTA_MODE_BYPASSED
            || mode == KIRIN_DELTA_MODE_PRE_INACTIVE;
    }

    // Record owns the six-cell layout. Without a published delta it keeps the delta placeholders;
    // an explicit PRE OFF state switches those same cells to POST absolute values.
    constexpr MetricMode recordMetricMode (bool, std::uint8_t mode) noexcept
    {
        return preUnavailableForDelta (mode)
            ? MetricMode::absolute : MetricMode::delta;
    }

    // A selected pair keeps its delta+MAX grid across transient missing/stale reads. Only an
    // explicit PRE OFF mode switches to POST absolute; no selected pair is always absolute.
    constexpr MetricMode watchMetricMode (bool pairSelected,
                                           bool,
                                           std::uint8_t mode) noexcept
    {
        if (! pairSelected)
            return MetricMode::absolute;
        if (preUnavailableForDelta (mode))
            return MetricMode::absolute;
        return MetricMode::delta;
    }
}
