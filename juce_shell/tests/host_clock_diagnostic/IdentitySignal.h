#pragma once
#include <cstdint>
#include <limits>

namespace hypha::clock_diagnostic
{
// NON-SHIPPING signal source, explicitly armed by the user in a disposable DAW fixture.
// Every stereo frame identifies an emission, not a looping project position. Zero is reserved
// for silence. Integer permutation and power-of-two scaling are exactly representable in f32.
// This is a diagnostic oracle, never a production alignment algorithm or audio watermark.
class IdentitySignal
{
public:
    static void sample (std::uint32_t id, float& left, float& right) noexcept
    {
        const auto packed = (id * 0x9e3779b1u) ^ 0x80008000u;
        left = static_cast<float> (static_cast<int> (packed & 0xffffu) - 32768) / 4194304.0f;
        right = static_cast<float> (static_cast<int> (packed >> 16) - 32768) / 4194304.0f;
    }

    void render (float* const* output, int channels, int frames, bool enabled) noexcept
    {
        for (int i = 0; i < frames; ++i)
        {
            float left = 0, right = 0;
            if (enabled && channels == 2 && next <= std::numeric_limits<std::uint32_t>::max())
                sample (static_cast<std::uint32_t> (next++), left, right);
            for (int c = 0; c < channels; ++c) output[c][i] = c == 0 ? left : right;
        }
    }

private:
    // Do not reset on stop, loop, seek, or prepareToPlay. Once exhausted, output silence;
    // never reuse an occurrence within an instance. A new instance has a new trace UUID.
    std::uint64_t next = 1;
};
}
