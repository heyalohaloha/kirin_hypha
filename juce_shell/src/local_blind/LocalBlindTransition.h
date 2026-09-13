#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace hypha::local_blind
{
// Audio-thread-owned de-click weights. Source gains remain fixed; only a symmetric, bounded
// transition at a selection/range boundary is mixed. No tail or prefix outside the cue is copied.
class LocalBlindTransition final
{
public:
    void beginPass() noexcept { initialized = false; }

    float nextSourceWeight (bool pre, std::uint32_t fadeFrames) noexcept
    {
        const float next = pre ? 1.0f : 0.0f;
        if (! initialized || fadeFrames == 0)
        {
            initialized = true;
            selectedPre = pre;
            target = weight = next;
            remaining = 0;
        }
        else if (selectedPre != pre)
        {
            selectedPre = pre;
            target = next;
            remaining = fadeFrames;
            step = (target - weight) / static_cast<float> (fadeFrames);
        }
        const auto result = weight;
        if (remaining > 0)
            weight = --remaining == 0 ? target : std::clamp (weight + step, 0.0f, 1.0f);
        return result;
    }

    static float edgeWeight (std::int64_t offset, std::int64_t frames,
                             std::uint32_t fadeFrames) noexcept
    {
        if (fadeFrames == 0) return 1.0f;
        const auto distance = std::min (offset, frames - 1 - offset);
        return std::min (1.0f, static_cast<float> (distance) / static_cast<float> (fadeFrames));
    }

    static float blend (float a, float b, float weight) noexcept
    {
        // Preserve exact equal-signal/signed-zero controls and avoid needless rounding at ends.
        if (weight <= 0.0f) return a;
        if (weight >= 1.0f) return b;
        if (std::memcmp (&a, &b, sizeof (float)) == 0) return a;
        return a * (1.0f - weight) + b * weight;
    }

private:
    bool initialized = false, selectedPre = false;
    float weight = 0.0f, target = 0.0f, step = 0.0f;
    std::uint32_t remaining = 0;
};
}
