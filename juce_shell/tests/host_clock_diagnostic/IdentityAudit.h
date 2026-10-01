#pragma once
#include <cmath>
#include <cstdint>

namespace hypha::clock_diagnostic
{
// NON-SHIPPING observer. Reverses the stereo encoding for EVERY frame, without consulting
// clocks, K, loop position or the product Consumer. A contiguous identity range then proves
// the intermediate PCM too, not just the two boundaries. No hashing/collision assumption.
struct IdentityAudit
{
    std::uint32_t first = 0, last = 0, frames = 0, silentPrefix = 0, errors = 0;

    static bool decode (float left, float right, std::uint32_t& id) noexcept
    {
        const double l = static_cast<double> (left) * 4194304.0 + 32768.0;
        const double r = static_cast<double> (right) * 4194304.0 + 32768.0;
        if (! std::isfinite (l) || ! std::isfinite (r) || l < 0 || l > 65535 || r < 0 || r > 65535
            || l != std::floor (l) || r != std::floor (r)) return false;
        id = (((static_cast<std::uint32_t> (r) << 16) | static_cast<std::uint32_t> (l))
              ^ 0x80008000u) * 0x0e8b2f51u;
        return id != 0;
    }

    static IdentityAudit inspect (const float* left, const float* right, int count) noexcept
    {
        IdentityAudit result;
        for (int i = 0; i < count; ++i)
        {
            if (left[i] == 0 && right[i] == 0 && result.frames == 0 && result.errors == 0)
            { ++result.silentPrefix; continue; }
            std::uint32_t id = 0;
            if (! decode (left[i], right[i], id)) { ++result.errors; continue; }
            if (result.frames == 0) result.first = id;
            else if (static_cast<std::uint64_t> (result.first) + result.frames != id) ++result.errors;
            result.last = id;
            ++result.frames;
        }
        return result;
    }
};
}
