#pragma once
#include <cstdint>

namespace hypha::live_compare
{
// Host-profile values, first measured in Studio Pro 8.1.2 and Pro Tools 2026.4. Both clock
// preparation and the PCM consumer use the same calibration policy, qualified per host/buffer.
struct CalibrationProfile
{
    std::int32_t streak = 8;
    bool invalidateOnDisagreement = true;
};
}
