#pragma once
#include "../HostProcessClock.h"
#include "../live_compare/LiveCompareSession.h"
#include <array>
#include <atomic>
#include <cstring>
#include <memory>

namespace hypha::chain_trace
{
constexpr int maximumFrames = 8192;
// Diagnostic observations only. Never consumed by the alignment, meter, Record or output path.
struct Row
{
    std::uint64_t serial = 0, localFrames = 0, entryNanos = 0, exitNanos = 0, reset = 0;
    double rate = 0;
    int role = 0, wrapper = 0, frames = 0, channels = 0;
    bool bypassed = false, offline = false, wallGap = false, outputTaken = false;
    bool blockSeen = false, timingSeen = false, renderSeen = false, feed = false;
    bool active = false, selected = false, blind = false, contentHeld = false;
    float preGain = 1, postGain = 1;
    int fixtureDelay = -1, fixtureReported = -1;
    std::uint64_t fixtureGeneration = 0;
    HostProcessClock host;
    live_compare::BlockClock block;
    live_compare::TimingSnapshot peer;
    live_compare::TimingEvidence timing;
    live_compare::Decision decision;
    live_compare::RenderReport render;
    // Planar input L/R, then actual output L/R. Unused channels are never written to disk.
    std::array<std::array<float, maximumFrames>, 4> pcm {}; // Non-RT construction touches every page.
};

// One serialised host callback producer, one private IO worker. Host callbacks may migrate
// threads but must not overlap for an instance (the processor itself has the same contract).
// A slot cannot be read until publish, or reused until pop. Full/oversize drops a WHOLE callback.
template<std::size_t Capacity = 64>
class Queue
{
public:
    Row* begin (int frames, int channels) noexcept
    {
        if (frames <= 0 || frames > maximumFrames || channels < 1 || channels > 2
            || write - read.load (std::memory_order_acquire) >= Capacity) return nullptr;
        return &rows[write % Capacity];
    }
    void publish() noexcept { published.store (++write, std::memory_order_release); }
    const Row* front() const noexcept
    {
        const auto index = read.load (std::memory_order_relaxed);
        return index == published.load (std::memory_order_acquire) ? nullptr : &rows[index % Capacity];
    }
    void pop() noexcept { read.fetch_add (1, std::memory_order_release); }
private:
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
    std::array<Row, Capacity> rows;
    std::uint64_t write = 0;
    std::atomic<std::uint64_t> read { 0 }, published { 0 };
};
}
