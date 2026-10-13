#pragma once
#include "AnalysisProductWait.h"
#include "../src/live_compare/LiveCompareClock.h"
#include <algorithm>
#include <ostream>

namespace hypha::tests
{
// Preserve the existing host callback sizes and audio budgets. ProductTestClock, not the
// scheduler, supplies their callback intervals. The cadence model below is only for
// synthetic jitter/gap regressions; real fixtures await worker receipts without a timer.
constexpr int analysisProductFrames (bool reaper) { return reaper ? 2048 : 256; }

inline AnalysisTestClock::time_point analysisProductBlockDeadline (
    AnalysisTestClock::time_point start, bool reaper)
{
    if (! reaper) return analysisBlockDeadline (start);
    // The exchange worker runs at 30 Hz WALL time, even when the synthetic audio runs faster.
    // 2048 / 96000 is 21.333 ms: retaining 5 ms pacing steals most of each recovery deadline
    // and can overrun the worker. Measure start-to-start: work consumes this period, not
    // extra time before it. An overrun starts a fresh period, never a catch-up burst.
    constexpr auto nanos = (std::uint64_t (analysisProductFrames (true)) * 1'000'000'000 + 95999) / 96000;
    return start + std::chrono::nanoseconds (nanos);
}

template <class Now, class SleepUntil, class Yield>
void modelHostCallbackCadence (AnalysisTestClock::time_point started, bool reaper,
                                 Now now, SleepUntil sleepUntil, Yield yield)
{
    const auto deadline = analysisProductBlockDeadline (started, reaper);
    if (now() < deadline) sleepUntil (deadline);
    else yield();
}

constexpr std::uint64_t analysisBlocksForSamples (std::uint64_t samples, std::uint64_t frames)
{ return (samples + frames - 1) / frames; }

struct AnalysisAudioDurations
{
    explicit constexpr AnalysisAudioDurations (bool reaper) : rate (reaper ? 96000 : 48000),
        linearMs (reaper ? 1120 : 2240), minimumDataMs (reaper ? 800 : 1600),
        loopMs (reaper ? 1000 : 2000), recoveryMs (reaper ? 750 : 1500) {}

    constexpr std::uint64_t samples (std::uint64_t milliseconds) const
    { return rate * milliseconds / 1000; }

    constexpr std::uint64_t recoverySamples (bool spectrum, std::uint64_t frames,
                                              std::uint64_t delay) const
    {
        if (rate != 96000 || spectrum) return samples (recoveryMs);
        // CertifiedProjectAnalysisClock drops the moved callback; there is no K calibration.
        // Bound the POST delay tail, wrap detection + discarded block + observation (3 blocks),
        // then the 30 Hz exchange: POST request -> PRE ready -> POST epoch, PRE publish -> POST join.
        // common_future_epoch leads by 2 x 100 ms, rounded UP to its 100 ms grid (<= 3 apertures).
        // SharpnessContinuousAnalyzer at 96 kHz needs TWO consecutive 100 ms input apertures
        // to release the first endpoint through ResamplerTo48k. No dropped input is budgeted.
        // Sources: spectrum_exchange_{post,pre,worker}.rs, analysis_exchange_protocol.rs,
        // perceptual.rs and resampler.rs. Round once to a whole callback, still from the true wrap.
        const auto bound = delay + 3 * frames + 5 * (rate / 30) + 5 * (rate / 10);
        return std::max (samples (recoveryMs), analysisBlocksForSamples (bound, frames) * frames);
    }

    constexpr std::int64_t recoveryDeadlineAfterWrap (std::int64_t before,
                                                       std::uint64_t recovery) const
    {
        const auto length = static_cast<std::int64_t> (samples (loopMs));
        return (before / length + 1) * length + static_cast<std::int64_t> (recovery);
    }

    // Preserve every host's previous audio span, independently of its callback size.
    const std::uint64_t rate, linearMs, minimumDataMs, loopMs, recoveryMs;
};

// A loop starts in PRE before the delayed POST wraps. Seeing has_data in that tail is NOT
// recovery. Require POST's actual invalid callback, then a complete window inside the new lap.
struct AnalysisWrapRecovery
{
    bool observe (bool postClockValid, bool hasData, std::int64_t endpoint,
                  std::uint64_t aperture, std::int64_t postEndInLap)
    {
        if (! postClockValid) invalidated = true;
        return invalidated && postClockValid && hasData && aperture > 0
            && endpoint >= static_cast<std::int64_t> (aperture) && endpoint <= postEndInLap;
    }
    bool invalidated = false;
};

struct AnalysisPostDelay
{
    int milliseconds = 0;
    std::uint64_t every = 1;

    bool due (std::uint64_t callbacks) const noexcept
    { return milliseconds > 0 && every > 0 && callbacks > 0 && callbacks % every == 0; }

    template <class Sleep> void beforePost (std::uint64_t callbacks, Sleep sleep)
    {
        if (! due (callbacks)) return;
        sleep (std::chrono::milliseconds (milliseconds));
        ++injections;
    }

    std::uint64_t injections = 0;
};

// Deliberate 12 ms inter-side jitter every third callback. The product fixture advances
// ProductTestClock by this amount; real scheduler load adds no unrequested host gap.
constexpr AnalysisPostDelay reaperAnalysisJitterDelay() { return { 12, 3 }; }

// Observe at the fixture's callback boundary using the unchanged production gap rule.
// These are boundary measurements, not access to the processor's private clock readings.
struct AnalysisCallbackIntervals
{
    void observe (AnalysisTestClock::time_point now, int frames, double rate)
    {
        const auto nanos = static_cast<std::uint64_t> (
            std::chrono::duration_cast<std::chrono::nanoseconds> (now.time_since_epoch()).count());
        if (havePrevious) maximumNanos = std::max (maximumNanos, nanos - previousNanos);
        if (detector.observe (nanos, frames, rate)) ++gaps;
        previousNanos = nanos;
        havePrevious = true;
    }

    void print (std::ostream& out, const char* role) const
    {
        out << role << " callback boundary: wall gaps " << gaps
            << ", maximum interval ms " << maximumNanos / 1.0e6 << '\n';
    }

    live_compare::GapDetector detector;
    std::uint64_t gaps = 0, maximumNanos = 0, previousNanos = 0;
    bool havePrevious = false;
};
}
