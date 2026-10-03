#pragma once
#include "kirin_hypha_ffi.h"
#include <limits>

namespace hypha::reference_audition
{
// Control-plane input for a new audition, never a frozen UI comparison value.
// Read the existing pre-audition meter; no decoder, observer or RT work is added.
struct LiveALevel
{
    double loudness = std::numeric_limits<double>::quiet_NaN();
    double peak = std::numeric_limits<double>::quiet_NaN();
    int windowBlocks = 0;  // A の直近の窓に入った 10 Hz の点の数（窓で測ったときだけ）
};

inline LiveALevel liveALevel (const KirinObservatoryFrame& frame, bool received,
                             bool callbackLive, bool playing) noexcept
{
    if (!received || !callbackLive || !playing || frame.signal_state != KIRIN_SIGNAL_STATE_ACTIVE
        || frame.meter.state != KIRIN_METER_SESSION_ACTIVE) return {};
    return { frame.meter.lufs_i, frame.meter.max_true_peak };
}
}
