#pragma once
#include "kirin_hypha_reference_visual_ffi.h"
#include "ReferenceTargetLoudnessWindow.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace hypha::reference_audition
{
// Worker-only TP facts for any whole-tick window. Keep zero-extended edges separate from
// continuous joins: neither an earlier sample nor a later sample belongs to the saved window.
// The pinned ebur128 FIR uses at most 24 input frames; 32 frames close its full support.
class TargetPeakWindow
{
    struct Tick { double start = 0, body = 0, join = 0, end = 0; };
    using Meter = std::unique_ptr<KirinReferenceVisualMeter, decltype (&kirin_reference_visual_drop)>;
public:
    void reset()
    { pcm.clear(); previous = {}; ticks.clear(); loudness.reset(); isolated.reset(); joined.reset(); rate = channels = 0; }
    void push (const float* input, int frames, int nextRate, int nextChannels)
    {
        if (nextRate != rate || nextChannels != channels)
        { reset(); rate = nextRate; channels = nextChannels; }
        const auto tickSamples = static_cast<size_t> ((rate / 10) * channels);
        for (size_t done = 0, total = static_cast<size_t> (frames * channels); done < total;)
        {
            const auto count = std::min (total - done, tickSamples - pcm.size());
            pcm.insert (pcm.end(), input + done, input + done + count); done += count;
            if (pcm.size() != tickSamples) continue;
            if (! finishTick())
            { reset(); rate = nextRate; channels = nextChannels; } // The failed tick ended here; keep the input's remaining samples aligned.
            std::fill (pcm.begin(), pcm.end(), 0.0f); pcm.clear(); // History holds numbers; only the FIR suffix survives.
        }
    }
    LiveWindowLoudness levels (int count) const { return loudness.window (count); }
    double window (int count) const
    {
        if (count < 1 || static_cast<size_t> (count) > ticks.size()) return std::numeric_limits<double>::quiet_NaN();
        const auto first = ticks.size() - static_cast<size_t> (count);
        double peak = std::max (ticks[first].start, ticks.back().end);
        for (size_t i = first; i < ticks.size(); ++i)
            peak = std::max ({ peak, ticks[i].body, i == first ? 0.0 : ticks[i].join });
        return 20.0 * std::log10 (std::max (peak, 1.0e-15));
    }
private:
    int rate = 0, channels = 0;
    std::vector<float> pcm;
    std::array<float, 64> previous {};
    std::vector<Tick> ticks;
    TargetLoudnessWindow loudness;
    Meter isolated { nullptr, kirin_reference_visual_drop }, joined { nullptr, kirin_reference_visual_drop };
    bool pushMeter (KirinReferenceVisualMeter* meter, const float* input, size_t count)
    {
        for (size_t done = 0; done < count;)
        {
            const auto chunk = std::min (static_cast<size_t> (4096 * channels), count - done);
            if (! kirin_reference_visual_push (meter, input + done, chunk)) return false;
            done += chunk;
        }
        return true;
    }
    static bool finish (KirinReferenceVisualMeter* meter, double& peak)
    {
        KirinReferenceVisualBin bin {};
        if (! kirin_reference_visual_finish (meter, &bin)) return false;
        peak = bin.true_peak; return true;
    }
    bool finishTick()
    {
        const auto edge = static_cast<size_t> (32 * channels);
        // Reuse bounded meters. The previous tick's zero tail resets isolated FIR support;
        // seeding the complete suffix replaces joined FIR support before its result is read.
        if (! isolated) isolated.reset (kirin_reference_visual_create (static_cast<std::uint32_t> (rate), static_cast<std::uint32_t> (channels)));
        if (! joined) joined.reset (kirin_reference_visual_create (static_cast<std::uint32_t> (rate), static_cast<std::uint32_t> (channels)));
        Tick tick;
        double discarded = 0;
        const std::array<float, 64> zeros {};
        if (! isolated || ! joined || pcm.size() < edge
            || ! pushMeter (isolated.get(), pcm.data(), edge) || ! finish (isolated.get(), tick.start)
            || ! pushMeter (isolated.get(), pcm.data() + edge, pcm.size() - edge) || ! finish (isolated.get(), tick.body)
            || ! pushMeter (isolated.get(), zeros.data(), edge) || ! finish (isolated.get(), tick.end)
            || ! pushMeter (joined.get(), previous.data(), edge) || ! finish (joined.get(), discarded)
            || ! pushMeter (joined.get(), pcm.data(), edge) || ! finish (joined.get(), tick.join)) return false;
        std::copy_n (pcm.end() - static_cast<std::ptrdiff_t> (edge), edge, previous.begin());
        if (ticks.size() == 6000) ticks.erase (ticks.begin());
        ticks.push_back (tick);
        loudness.push (pcm.data(), rate, channels);
        return true;
    }
};
}
