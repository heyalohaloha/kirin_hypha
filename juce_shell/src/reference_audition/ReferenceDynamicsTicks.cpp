#include "ReferenceDynamicsRange.h"

#include <algorithm>

// A の 100 ms の bin を貯める部品（Rust の VisualMeter を使う。範囲の計算そのものは ReferenceDynamicsRange.cpp）。
namespace hypha::reference_audition
{
DynamicsTicks::~DynamicsTicks() { kirin_reference_visual_drop (meter); }

void DynamicsTicks::reset() noexcept
{
    kirin_reference_visual_drop (meter);
    meter = nullptr;
    fill = 0;
    held.clear();
    published.reset();
}

void DynamicsTicks::push (const float* interleaved, int frames, int rate, int channels)
{
    if (interleaved == nullptr || frames <= 0 || rate < 8'000 || rate > 768'000 || channels < 1 || channels > 2) return;
    if (meter == nullptr || rate != meterRate || channels != meterChannels)
    {
        reset();
        meter = kirin_reference_visual_create (static_cast<std::uint32_t> (rate), static_cast<std::uint32_t> (channels));
        if (meter == nullptr) return;
        meterRate = rate;
        meterChannels = channels;
    }
    const auto tick = rate / 10;
    for (int done = 0; done < frames;)
    {
        const auto count = std::min (frames - done, tick - fill);
        if (! kirin_reference_visual_push (meter, interleaved + static_cast<std::size_t> (done) * static_cast<std::size_t> (channels),
                                           static_cast<std::size_t> (count) * static_cast<std::size_t> (channels)))
        { reset(); return; }
        done += count;
        fill += count;
        if (fill < tick) continue;
        KirinReferenceVisualBin bin {};
        if (! kirin_reference_visual_finish (meter, &bin)) { reset(); return; }
        fill = 0;
        if (static_cast<int> (held.size()) >= capacity) held.erase (held.begin());
        held.push_back (bin);
        published = std::make_shared<const std::vector<KirinReferenceVisualBin>> (held);
    }
}
}
