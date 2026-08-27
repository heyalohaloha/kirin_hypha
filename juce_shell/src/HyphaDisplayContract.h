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

    // During a live Keep the current pairing is frozen by the engine. Once stopped, only the
    // exact PRE captured by that Keep may own its held delta; changing pairs falls back to the
    // POST's absolute held result without discarding I.
    constexpr bool recordPairContext (bool recording,
                                      bool pairSelected,
                                      bool haveHeldDelta,
                                      bool heldPairMatchesCurrent) noexcept
    {
        return recording ? pairSelected
                         : haveHeldDelta && heldPairMatchesCurrent;
    }

    // Pair identity owns the six-cell layout; signal availability only owns the values/muting.
    // This prevents PRE silence/bypass/stale reads from presenting as a detached pair.
    constexpr MetricMode recordMetricMode (bool pairSelected,
                                            bool,
                                            std::uint8_t) noexcept
    {
        return pairSelected ? MetricMode::delta : MetricMode::absolute;
    }

    // Watch follows the same separation: once selected, the pair keeps its delta+MAX grid until
    // the user actually unpairs it. Unavailable PRE measurements render muted/empty in that grid.
    constexpr MetricMode watchMetricMode (bool pairSelected,
                                           bool,
                                           std::uint8_t) noexcept
    {
        return pairSelected ? MetricMode::delta : MetricMode::absolute;
    }
}
