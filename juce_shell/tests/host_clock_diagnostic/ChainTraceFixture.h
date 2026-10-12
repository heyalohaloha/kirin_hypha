#pragma once
#include "IdentitySignal.h"
#include <array>
#include <cstdint>

namespace hypha::chain_trace_fixture
{
// Only for the separately named, explicitly armed fixture plug-in. These samples never enter
// the product through a hidden generator. Monotonic IDs do not restart on loop/seek/prepare.
class Source
{
public:
    void process (float* const* output, int channels, int frames, bool armed) noexcept
    {
        for (int f = 0; f < frames; ++f)
        {
            float l = 0, r = 0;
            if (armed && channels == 2 && next <= UINT32_MAX)
                clock_diagnostic::IdentitySignal::sample (static_cast<std::uint32_t> (next++), l, r);
            else if (armed && channels == 1 && next <= 4194303)
                l = static_cast<float> (next++) / 536870912.0f;
            for (int c = 0; c < channels; ++c) output[c][f] = c == 0 ? l : r;
        }
    }
private:
    std::uint64_t next = 1;
};

class Delay
{
public:
    static constexpr int maximum = 65536;
    void reset() noexcept { for (auto& channel : samples) channel.fill (0); cursor = 0; }
    void process (float* const* io, int channels, int frames, int delay) noexcept
    {
        if (delay < 0 || delay > maximum || channels < 1 || channels > 2) return;
        for (int f = 0; f < frames; ++f)
        {
            const auto read = (cursor + capacity - static_cast<unsigned> (delay)) % capacity;
            for (int c = 0; c < channels; ++c)
            {
                samples[static_cast<std::size_t> (c)][cursor] = io[c][f];
                io[c][f] = samples[static_cast<std::size_t> (c)][read];
            }
            cursor = (cursor + 1) % capacity;
        }
    }
private:
    static constexpr unsigned capacity = maximum + 1;
    std::array<std::array<float, capacity>, 2> samples {};
    unsigned cursor = 0;
};
}
