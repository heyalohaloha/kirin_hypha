#pragma once
#include "LiveCompareRing.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

#if defined (__APPLE__)
 #include <pthread.h>
#elif defined (_WIN32)
extern "C" __declspec (dllimport) unsigned long __stdcall GetCurrentThreadId (void);
#endif

namespace hypha::live_compare
{
// Chain timing: how long the host spent between PRE's callback and POST's callback, which is the
// plug-ins the user put between them plus whatever else the host ran there. It is elapsed
// wall-clock time, set against the block's own length the way JUCE's AudioProcessLoadMeasurer and
// DAW meters do. It is not CPU time and not a deadline, and it is display only: it never selects a
// source, proves a sample correspondence or changes audio.
//
// A block is counted only when the facts show that PRE's callback for this host cycle returned,
// on this thread, just before POST's began: the same thread, exactly one PRE callback since
// POST's previous one, equal block lengths, and project positions that put PRE first while both
// play. Any other block is reported as the reason it was not counted, never as a number.
//
// Each side uses the one wall-clock reading its callback already takes, and PRE's travels in the
// timing header it already writes, so the measurement adds no clock reading, thread, queue or
// mapping to either Audio Thread.

// steady_clock is one system-wide monotonic clock on macOS and Windows, so readings taken by two
// instances compare.
inline std::uint64_t callbackWallNanos() noexcept
{
    return static_cast<std::uint64_t> (std::chrono::duration_cast<std::chrono::nanoseconds> (
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// A system-wide thread identity read without a kernel call, so that equal values mean one thread
// of one process.
inline std::uint64_t callbackThread() noexcept
{
   #if defined (__APPLE__)
    std::uint64_t identity = 0;
    pthread_threadid_np (nullptr, &identity);
    return identity;
   #elif defined (_WIN32)
    return static_cast<std::uint64_t> (GetCurrentThreadId());
   #else
    static thread_local char marker = 0; // one address for each thread of this process
    return static_cast<std::uint64_t> (reinterpret_cast<std::uintptr_t> (&marker));
   #endif
}

enum class ChainTimingReason : std::uint8_t
{
    none,          // counted
    noPre,         // PRE's timing could not be read in this callback
    notPlaying,    // PRE or POST is not playing with a project position
    preFeeding,    // PRE copied its input for a live session after its clock reading
    otherThread,   // PRE's callback ran on another thread, or overlapped this one
    unevenCalls,   // PRE ran more or less than once since POST's previous callback
    unevenBlocks,  // the two callbacks carry different block lengths
    orderUnproven  // the positions do not put PRE first in this cycle
};
constexpr std::size_t chainTimingReasons = 8;
constexpr std::uint64_t chainWindowNanos = 500'000'000;
constexpr std::size_t chainHistoryWindows = 10; // a report covers about the last five seconds

// The blocks of the recent windows, added up. The sums are of counted blocks only.
struct ChainTimingReport
{
    std::uint64_t serial = 0, endNanos = 0; // serial 0: nothing was published yet
    std::uint32_t callbacks = 0, blocks = 0;
    std::uint64_t elapsedNanos = 0, frames = 0;
    std::uint64_t peakNanos = 0; // the block with the largest elapsed time for its length
    std::uint32_t peakFrames = 0;
    std::array<std::uint32_t, chainTimingReasons> rejected {};
};

// POST. The Audio Thread observes every callback and closes a window about twice a second; any
// other thread reads the last report. Nothing here waits, allocates or calls the system.
class ChainTimingMeter
{
public:
    // Audio Thread, for a callback that read PRE's timing coherently. fedBlocks is PRE's count
    // of blocks copied for a live session: a copy follows PRE's clock reading, so a block PRE
    // fed since POST's previous callback would count PRE's own work as chain time.
    void observe (const TimingSnapshot& pre, const BlockClock& post, std::uint64_t fedBlocks) noexcept
    {
        account (post, classify (pre, post, fedBlocks), pre.block.wallNanos);
        haveSequence = true;
        sequence = pre.sequence; ownerA = pre.ownerA; ownerB = pre.ownerB;
        fed = fedBlocks;
    }

    // Audio Thread, for a callback with no readable PRE timing.
    void observeWithoutPre (const BlockClock& post) noexcept
    {
        account (post, ChainTimingReason::noPre, 0);
        haveSequence = false;
    }

    // Any other thread. False when no consistent report could be read: the caller keeps its last.
    bool read (ChainTimingReport& out) const noexcept
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto seq = published.sequence.load (std::memory_order_acquire);
            if ((seq & 1u) != 0) continue;
            ChainTimingReport next;
            next.serial = published.serial.load (std::memory_order_relaxed);
            next.endNanos = published.endNanos.load (std::memory_order_relaxed);
            next.callbacks = published.callbacks.load (std::memory_order_relaxed);
            next.blocks = published.blocks.load (std::memory_order_relaxed);
            next.elapsedNanos = published.elapsedNanos.load (std::memory_order_relaxed);
            next.frames = published.frames.load (std::memory_order_relaxed);
            next.peakNanos = published.peakNanos.load (std::memory_order_relaxed);
            next.peakFrames = published.peakFrames.load (std::memory_order_relaxed);
            for (std::size_t i = 0; i < chainTimingReasons; ++i)
                next.rejected[i] = published.rejected[i].load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq != published.sequence.load (std::memory_order_relaxed)) continue;
            out = next;
            return true;
        }
        return false;
    }

private:
    struct Window
    {
        std::uint64_t endNanos = 0, elapsedNanos = 0, frames = 0, peakNanos = 0;
        std::uint32_t callbacks = 0, blocks = 0, peakFrames = 0;
        std::array<std::uint32_t, chainTimingReasons> rejected {};
    };
    struct Published
    {
        std::atomic<std::uint64_t> sequence { 0 }, serial { 0 }, endNanos { 0 };
        std::atomic<std::uint32_t> callbacks { 0 }, blocks { 0 }, peakFrames { 0 };
        std::atomic<std::uint64_t> elapsedNanos { 0 }, frames { 0 }, peakNanos { 0 };
        std::array<std::atomic<std::uint32_t>, chainTimingReasons> rejected {};
    };

    // elapsed / frames compared without a division; a block of no frames is never a peak.
    static bool longerForItsLength (std::uint64_t elapsed, std::uint32_t frames,
                                    std::uint64_t peak, std::uint32_t peakFrames) noexcept
    {
        return frames > 0 && (peakFrames == 0 || elapsed * peakFrames > peak * frames);
    }

    ChainTimingReason classify (const TimingSnapshot& pre, const BlockClock& post,
                                std::uint64_t fedBlocks) const noexcept
    {
        if (! pre.active || ! post.playing || ! post.projectValid || post.frames <= 0)
            return ChainTimingReason::notPlaying;
        if (haveSequence && fedBlocks != fed)
            return ChainTimingReason::preFeeding;
        // One thread runs its callbacks in order, so a stamp from this thread is never later
        // than this callback's own reading.
        if (pre.block.thread != post.thread || post.wallNanos < pre.block.wallNanos)
            return ChainTimingReason::otherThread;
        if (! haveSequence || pre.ownerA != ownerA || pre.ownerB != ownerB
            || pre.sequence != sequence + 2)
            return ChainTimingReason::unevenCalls;
        if (pre.block.frames != post.frames)
            return ChainTimingReason::unevenBlocks;
        // A POST placed before PRE reads the stamp PRE left in the previous cycle, one block
        // behind. Only a position that advances exactly can tell the two cases apart, so a
        // stopped, looping-back or seeking block is not counted.
        std::int64_t expected = 0, lead = 0;
        if (! previousMoving || ! checkedClockAdd (previousProject, previousFrames, expected)
            || post.project != expected
            || ! checkedClockSubtract (pre.block.project, post.project, lead)
            || lead < 0 || lead > static_cast<std::int64_t> (ringCapacityFrames))
            return ChainTimingReason::orderUnproven;
        return ChainTimingReason::none;
    }

    void account (const BlockClock& post, ChainTimingReason reason, std::uint64_t preNanos) noexcept
    {
        if (post.wallNanos == 0)
            return; // no reading, no window: a fixture that drives no wall clock
        // After a pause in POST's callbacks the open window describes the time before it.
        if (windowStart == 0 || post.wallNanos - windowStart > 2 * chainWindowNanos)
        {
            current = {};
            windowStart = post.wallNanos;
        }
        ++current.callbacks;
        if (reason == ChainTimingReason::none)
        {
            const auto elapsed = post.wallNanos - preNanos;
            const auto frames = static_cast<std::uint32_t> (post.frames);
            ++current.blocks;
            current.elapsedNanos += elapsed;
            current.frames += frames;
            if (longerForItsLength (elapsed, frames, current.peakNanos, current.peakFrames))
            {
                current.peakNanos = elapsed;
                current.peakFrames = frames;
            }
        }
        else
            ++current.rejected[static_cast<std::size_t> (reason)];
        previousMoving = post.playing && post.projectValid && post.frames > 0;
        previousProject = post.project;
        previousFrames = post.frames;
        if (post.wallNanos - windowStart >= chainWindowNanos)
            close (post.wallNanos);
    }

    void close (std::uint64_t now) noexcept
    {
        current.endNanos = now;
        history[cursor] = current;
        cursor = (cursor + 1) % chainHistoryWindows;
        current = {};
        windowStart = now;
        Window total;
        for (const auto& window : history)
        {
            // A window from before a pause is not part of the recent time.
            if (window.endNanos == 0
                || now - window.endNanos > (chainHistoryWindows + 1) * chainWindowNanos)
                continue;
            total.callbacks += window.callbacks;
            total.blocks += window.blocks;
            total.elapsedNanos += window.elapsedNanos;
            total.frames += window.frames;
            if (longerForItsLength (window.peakNanos, window.peakFrames, total.peakNanos, total.peakFrames))
            {
                total.peakNanos = window.peakNanos;
                total.peakFrames = window.peakFrames;
            }
            for (std::size_t i = 0; i < chainTimingReasons; ++i)
                total.rejected[i] += window.rejected[i];
        }
        const auto seq = published.sequence.load (std::memory_order_relaxed);
        published.sequence.store (seq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        published.serial.store (published.serial.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        published.endNanos.store (now, std::memory_order_relaxed);
        published.callbacks.store (total.callbacks, std::memory_order_relaxed);
        published.blocks.store (total.blocks, std::memory_order_relaxed);
        published.elapsedNanos.store (total.elapsedNanos, std::memory_order_relaxed);
        published.frames.store (total.frames, std::memory_order_relaxed);
        published.peakNanos.store (total.peakNanos, std::memory_order_relaxed);
        published.peakFrames.store (total.peakFrames, std::memory_order_relaxed);
        for (std::size_t i = 0; i < chainTimingReasons; ++i)
            published.rejected[i].store (total.rejected[i], std::memory_order_relaxed);
        published.sequence.store (seq + 2, std::memory_order_release);
    }

    // Audio Thread only.
    Window current;
    std::array<Window, chainHistoryWindows> history {};
    std::size_t cursor = 0;
    std::uint64_t windowStart = 0, sequence = 0, ownerA = 0, ownerB = 0, fed = 0;
    std::int64_t previousProject = 0;
    std::int32_t previousFrames = 0;
    bool haveSequence = false, previousMoving = false;
    Published published;
};

// What the screen says about a report. Times are per block; a load is elapsed time over the
// block's own length, so 0.5 means the chain took half as long as the block lasts.
struct ChainTimingView
{
    enum class State : std::uint8_t { waiting, unavailable, measuring };
    State state = State::waiting;
    ChainTimingReason reason = ChainTimingReason::none; // the most frequent reason not counted
    double typicalMs = 0.0, peakMs = 0.0, blockMs = 0.0;
    double typicalLoad = 0.0, peakLoad = 0.0;
    double countedShare = 0.0; // counted blocks over POST callbacks
};

inline ChainTimingView summarise (const ChainTimingReport& report, double rate,
                                  std::uint64_t nowNanos) noexcept
{
    ChainTimingView view;
    // Without recent callbacks there is nothing current to show, whatever was measured before.
    const bool stale = nowNanos > report.endNanos && nowNanos - report.endNanos > 3 * chainWindowNanos;
    if (report.serial == 0 || report.callbacks == 0 || stale || ! (rate > 0.0))
        return view;
    std::size_t most = 0;
    for (std::size_t i = 1; i < chainTimingReasons; ++i)
        if (report.rejected[i] > report.rejected[most]) most = i;
    view.reason = report.rejected[most] > 0 ? static_cast<ChainTimingReason> (most)
                                            : ChainTimingReason::none;
    view.countedShare = static_cast<double> (report.blocks) / static_cast<double> (report.callbacks);
    if (report.blocks == 0 || report.frames == 0 || report.peakFrames == 0)
    {
        view.state = ChainTimingView::State::unavailable;
        return view;
    }
    const auto blocks = static_cast<double> (report.blocks);
    const auto elapsed = static_cast<double> (report.elapsedNanos);
    const auto audioNanos = static_cast<double> (report.frames) / rate * 1.0e9;
    const auto peakAudioNanos = static_cast<double> (report.peakFrames) / rate * 1.0e9;
    view.state = ChainTimingView::State::measuring;
    view.typicalMs = elapsed / blocks / 1.0e6;
    view.blockMs = audioNanos / blocks / 1.0e6;
    view.typicalLoad = elapsed / audioNanos;
    view.peakMs = static_cast<double> (report.peakNanos) / 1.0e6;
    view.peakLoad = static_cast<double> (report.peakNanos) / peakAudioNanos;
    return view;
}
}
