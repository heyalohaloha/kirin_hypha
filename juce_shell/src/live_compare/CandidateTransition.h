#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace hypha::live_compare
{
// A discontinuity has no valid old PCM to crossfade. Interpolate from the last output
// value to the current pass-through over 5 ms; never replay an old block at a new time.
// Two scalars per channel are allocated with the renderer, outside the callback.
class CandidateTransition
{
public:
    bool pending() const noexcept { return remaining > 0; }
    void reset() noexcept { remaining = 0; seen = substituted = false; }
    bool apply (float* const* io, int channels, int frames, int fadeFrames,
                bool valid, bool changed, bool preAudible = true) noexcept
    {
        if (io == nullptr || channels < 1 || channels > 2 || frames < 1) return false;
        if (seen && substituted && (! valid || changed))
        {
            origin = last;
            remaining = fadeFrames;
        }
        const bool transitioning = remaining > 0;
        for (int i = 0; i < frames && remaining > 0; ++i, --remaining)
        {
            const auto weight = 1.0f - static_cast<float> (remaining) / static_cast<float> (fadeFrames);
            for (int ch = 0; ch < channels; ++ch)
                io[ch][i] = origin[static_cast<std::size_t> (ch)] * (1.0f - weight) + io[ch][i] * weight;
        }
        for (int ch = 0; ch < channels; ++ch) last[static_cast<std::size_t> (ch)] = io[ch][frames - 1];
        seen = std::isfinite (last[0]) && (channels == 1 || std::isfinite (last[1]));
        substituted = valid && preAudible;
        return transitioning;
    }
private:
    std::array<float, 2> last {}, origin {};
    int remaining = 0;
    bool seen = false, substituted = false;
};
}
