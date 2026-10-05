#pragma once
#include "../../src/live_compare/LiveCompareChainTiming.h"
#include "../../src/live_compare/LiveCompareSession.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

// INV-LC25. One PRE and one POST on a simulated host, through the product's own feeder, timing
// header and meter. The wall clock and the threads are the fixture's, so every case is exact.
namespace chain_timing_test
{
using namespace hypha::live_compare;
inline void require (bool value, const char* message)
{
    if (! value) { std::fprintf (stderr, "chain timing: %s\n", message); std::exit (1); }
}

constexpr double rate = 48000.0;
constexpr std::uint64_t thisThread = 41, anotherThread = 42;

// Each cycle PRE publishes as its Audio Thread does, and POST reads what its Audio Thread reads.
struct Host
{
    std::unique_ptr<Ring> ring = std::make_unique<Ring>();
    PreFeeder feeder;
    ChainTimingMeter meter;
    std::int64_t position = 96000;
    std::uint64_t now = 1'000'000'000;
    std::int32_t frames = 128;
    std::int64_t preClock = 0;
    bool playing = true;
    std::array<std::array<float, 128>, 2> input {};

    Host() { ring->initialise (0x20261005, static_cast<std::uint32_t> (rate)); }

    BlockClock block (std::int64_t project, std::int32_t length, std::uint64_t wall,
                      std::uint64_t thread) const
    {
        BlockClock result;
        result.project = project;
        result.frames = length;
        result.projectValid = result.clockValid = true;
        result.playing = playing;
        result.wallNanos = wall;
        result.thread = thread;
        return result;
    }
    std::uint64_t period() const
    { return static_cast<std::uint64_t> (static_cast<double> (frames) / rate * 1.0e9); }

    void pre (std::int64_t project, std::uint64_t thread = thisThread, std::int32_t length = 0)
    {
        auto clock = block (project, length > 0 ? length : frames, now, thread);
        clock.clock = preClock;
        preClock += clock.frames;
        const float* const channels[] { input[0].data(), input[1].data() };
        feeder.feed (*ring, clock, channels, 2);
    }
    void post (std::int64_t project, std::uint64_t elapsed, std::uint64_t thread = thisThread)
    {
        TimingSnapshot snapshot;
        const auto clock = block (project, frames, now + elapsed, thread);
        if (readTiming (ring->header.timing, snapshot))
            meter.observe (snapshot, clock, ring->header.published.load (std::memory_order_acquire));
        else
            meter.observeWithoutPre (clock);
    }
    // PRE, the chain, then POST, as one thread runs one track's inserts. lead is how far ahead of
    // POST the host reports PRE's position (delay compensation).
    void cycle (std::uint64_t elapsed, std::int64_t lead = 0)
    {
        pre (position + lead);
        post (position, elapsed);
        advance();
    }
    // POST placed before PRE: it reads the stamp PRE left in the previous cycle.
    void reversedCycle (std::uint64_t elapsed)
    {
        post (position, elapsed);
        pre (position);
        advance();
    }
    void advance() { position += frames; now += period(); }

    ChainTimingReport report() const
    {
        ChainTimingReport result;
        require (meter.read (result), "a quiet report reads consistently");
        return result;
    }
    std::uint32_t rejected (ChainTimingReason reason) const
    { return report().rejected[static_cast<std::size_t> (reason)]; }
};

constexpr int cyclesPerSecond = 375; // 128 frames at 48 kHz
constexpr int cycles = 800;          // a little over two seconds: four closed windows

inline void countsTheChainOfOneThread()
{
    Host host;
    for (int i = 0; i < cycles; ++i) host.cycle (i == 200 ? 900'000u : 400'000u);
    const auto report = host.report();
    require (report.serial >= 1 && report.blocks > 300, "windows close about twice a second");
    require (report.rejected[static_cast<std::size_t> (ChainTimingReason::unevenCalls)] == 1
                 && report.blocks + 1 == report.callbacks,
             "only the first block, which has no previous callback to stand on, is not counted");
    require (report.frames == static_cast<std::uint64_t> (report.blocks) * 128u, "block lengths are summed");
    require (report.elapsedNanos == static_cast<std::uint64_t> (report.blocks - 1) * 400'000u + 900'000u,
             "the sum is POST's reading minus PRE's stamp, block by block");
    require (report.peakNanos == 900'000u && report.peakFrames == 128u, "the peak block is kept exactly");
    const auto view = summarise (report, rate, report.endNanos);
    require (view.state == ChainTimingView::State::measuring, "counted blocks are a measurement");
    require (std::abs (view.blockMs - 128.0 / 48.0) < 1.0e-9, "the block's own length comes from its frames");
    require (std::abs (view.peakMs - 0.9) < 1.0e-9 && std::abs (view.peakLoad - 0.9 / (128.0 / 48.0)) < 1.0e-9,
             "a load is elapsed time over the block's length");
    require (view.typicalMs > 0.4 && view.typicalMs < 0.41 && view.typicalLoad > 0.15 && view.typicalLoad < 0.155,
             "the typical value is the mean of the counted blocks");
    require (view.countedShare > 0.99 && view.countedShare < 1.0, "the counted share is reported");
}

inline void aChainWithDelayCompensationIsCounted()
{
    Host host;
    for (int i = 0; i < cycles; ++i) host.cycle (250'000u, 4096);
    require (host.report().blocks > 300, "PRE ahead of POST by the chain's delay is still PRE first");
}

inline void postBeforePreIsNeverCounted()
{
    Host host;
    host.pre (host.position - host.frames); // PRE has run before; POST still precedes it each cycle
    for (int i = 0; i < cycles; ++i) host.reversedCycle (2'000'000u);
    const auto report = host.report();
    require (report.blocks == 0 && report.callbacks > 300, "a stamp from the previous cycle is not this chain");
    require (report.rejected[static_cast<std::size_t> (ChainTimingReason::orderUnproven)] + 1 >= report.callbacks,
             "the reason is the order");
    const auto view = summarise (report, rate, report.endNanos);
    require (view.state == ChainTimingView::State::unavailable
                 && view.reason == ChainTimingReason::orderUnproven, "no number is shown for it");
}

inline void aLoopOrSeekBlockIsNotCounted()
{
    Host host;
    for (int i = 0; i < cycles; ++i)
    {
        if (i == 100 || i == 250) host.position = 96000; // the position jumps back
        host.cycle (300'000u);
    }
    const auto report = host.report();
    require (report.rejected[static_cast<std::size_t> (ChainTimingReason::orderUnproven)] == 2
                 && report.blocks + 3 == report.callbacks,
             "each jump costs exactly its own block");
    // A reversed pair looks ordered only on the block where the position jumps back.
    Host reversed;
    reversed.pre (reversed.position - reversed.frames);
    for (int i = 0; i < cycles; ++i)
    {
        if (i % 50 == 49) reversed.position = 96000;
        reversed.reversedCycle (2'000'000u);
    }
    require (reversed.report().blocks == 0, "a jump never makes a reversed pair count");
}

inline void otherThreadsAndUnevenCallsAreNotCounted()
{
    Host host;
    for (int i = 0; i < cycles; ++i)
    {
        host.pre (host.position, anotherThread);
        host.post (host.position, 300'000u);
        host.advance();
    }
    require (host.report().blocks == 0 && host.rejected (ChainTimingReason::otherThread) > 300,
             "PRE on another thread is not this thread's chain");

    Host twice;
    for (int i = 0; i < cycles; ++i)
    {
        twice.pre (twice.position - twice.frames);
        twice.pre (twice.position);
        twice.post (twice.position, 300'000u);
        twice.advance();
    }
    require (twice.report().blocks == 0 && twice.rejected (ChainTimingReason::unevenCalls) > 300,
             "two PRE callbacks for one POST callback are not one cycle");

    Host split;
    for (int i = 0; i < cycles; ++i)
    {
        split.pre (split.position, thisThread, 64);
        split.post (split.position, 300'000u);
        split.advance();
    }
    require (split.report().blocks == 0 && split.rejected (ChainTimingReason::unevenBlocks) > 300,
             "a host that splits the block differently for PRE and POST is not counted");

    Host overlapped;
    for (int i = 0; i < cycles; ++i)
    {
        overlapped.now += 500'000u; // PRE's stamp is later than POST's reading
        overlapped.pre (overlapped.position);
        overlapped.now -= 500'000u;
        overlapped.post (overlapped.position, 0);
        overlapped.advance();
    }
    require (overlapped.report().blocks == 0 && overlapped.rejected (ChainTimingReason::otherThread) > 300,
             "a stamp later than POST's own reading did not come from this thread's order");
}

inline void stoppedAndFeedingAreNotCounted()
{
    Host host;
    host.playing = false;
    for (int i = 0; i < cycles; ++i)
    {
        host.pre (host.position);
        host.post (host.position, 300'000u);
        host.now += host.period(); // the position stays
    }
    require (host.report().blocks == 0 && host.rejected (ChainTimingReason::notPlaying) > 300,
             "a stopped transport cannot show which callback came first");

    Host feeding;
    feeding.ring->header.demand.store (1);
    for (int i = 0; i < cycles; ++i) feeding.cycle (300'000u);
    require (feeding.report().blocks == 0 && feeding.rejected (ChainTimingReason::preFeeding) > 300,
             "a block PRE copied for a live session after its clock reading is not chain time");
    feeding.ring->header.demand.store (0);
    for (int i = 0; i < 12 * cyclesPerSecond / 2; ++i) feeding.cycle (300'000u);
    require (feeding.report().blocks > 300 && feeding.rejected (ChainTimingReason::preFeeding) == 0,
             "the measurement returns when PRE stops copying, and old windows leave the report");
}

inline void aMissingPreAndAPauseAreNotMeasurements()
{
    Host host;
    for (int i = 0; i < cycles; ++i)
    {
        host.meter.observeWithoutPre (host.block (host.position, host.frames, host.now, thisThread));
        host.advance();
    }
    auto report = host.report();
    auto view = summarise (report, rate, report.endNanos);
    require (view.state == ChainTimingView::State::unavailable && view.reason == ChainTimingReason::noPre,
             "no PRE timing is a reason, not a zero");

    ChainTimingMeter silent;
    ChainTimingReport nothing;
    require (silent.read (nothing) && summarise (nothing, rate, 5'000'000'000u).state == ChainTimingView::State::waiting,
             "before any window there is nothing to show");

    Host paused;
    for (int i = 0; i < cycles; ++i) paused.cycle (300'000u);
    report = paused.report();
    require (summarise (report, rate, report.endNanos + 4 * chainWindowNanos).state == ChainTimingView::State::waiting,
             "a report older than the recent callbacks is not shown as current");
    paused.now += 20'000'000'000u; // the host stops calling for a while
    for (int i = 0; i < cycles; ++i) paused.cycle (700'000u);
    report = paused.report();
    require (report.blocks > 0 && report.elapsedNanos == static_cast<std::uint64_t> (report.blocks) * 700'000u,
             "windows from before a pause do not mix into the report after it");
}

inline void theCallbackReadingsAreUsable()
{
    const auto first = callbackWallNanos();
    const auto second = callbackWallNanos();
    require (first != 0 && second >= first, "the wall clock is monotonic");
    require (callbackThread() != 0 && callbackThread() == callbackThread(), "a thread keeps one identity");
}
inline void run()
{
    countsTheChainOfOneThread();
    aChainWithDelayCompensationIsCounted();
    postBeforePreIsNeverCounted();
    aLoopOrSeekBlockIsNotCounted();
    otherThreadsAndUnevenCallsAreNotCounted();
    stoppedAndFeedingAreNotCounted();
    aMissingPreAndAPauseAreNotMeasurements();
    theCallbackReadingsAreUsable();
    std::puts ("chain timing: counted, refused and reported cases PASS");
}
}
