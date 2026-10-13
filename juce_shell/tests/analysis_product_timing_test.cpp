#include "AnalysisProductTiming.h"
#include "../src/live_compare/CertifiedProjectAnalysisClock.h"
#include <iostream>
#include <stdexcept>

namespace
{
void require (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); }
using namespace hypha::tests;
using namespace std::chrono;

void reaperPacingDoesNotOutrunTheExchange()
{
    const auto start = AnalysisTestClock::time_point {};
    const auto period = analysisProductBlockDeadline (start, true) - start;
    const auto audio = duration<double> (analysisProductFrames (true) / 96000.0);
    require (period >= audio, "REAPER fixture must not feed audio faster than realtime exchange");
    require (period - audio < nanoseconds (1), "callback pacing rounds up by less than one nanosecond");
    require (analysisProductBlockDeadline (start, false) == analysisBlockDeadline (start),
             "other host fixture pacing is unchanged");
    // At the old 5 ms cadence, a 750 ms audio deadline allows only 175 ms of wall time.
    // Even three 30 Hz handshake ticks + two publication ticks consume almost all of it,
    // before the future epoch and continuous-state apertures can be completed.
    const auto blocks = 72000 / analysisProductFrames (true);
    require (blocks * period >= milliseconds (740), "the audio deadline must leave real exchange time");
    const auto lateStart = start + seconds (1);
    require (analysisProductBlockDeadline (lateStart, true) == lateStart + period,
             "a late callback starts its own period, with no catch-up burst");
}

void loopRecoveryRejectsOldData()
{
    AnalysisWrapRecovery wrap;
    require (! wrap.observe (true, true, 92800, 8192, 94976), "POST tail before its wrap is not recovery");
    require (! wrap.observe (false, false, 0, 0, 2048), "invalid POST is not recovery");
    require (! wrap.observe (true, true, 92800, 8192, 12288), "old-lap endpoint is not recovery");
    require (! wrap.observe (true, true, 6400, 8192, 12288), "partial aperture is not recovery");
    require (! wrap.observe (true, false, 9600, 8192, 12288), "missing data is not recovery");
    require (wrap.observe (true, true, 9600, 8192, 12288), "first complete fresh zero delta can recover");
    wrap = {};
    require (! wrap.observe (true, true, 9600, 8192, 12288), "each lap needs its own invalidation");
}

void durationBudgetsAndGapBoundaries()
{
    for (const auto reaper : { false, true })
    {
        const AnalysisAudioDurations audio (reaper);
        const auto frames = static_cast<std::uint64_t> (analysisProductFrames (reaper));
        require (audio.samples (audio.linearMs) == 420 * 256, "linear audio span is preserved");
        require (audio.samples (audio.minimumDataMs) == 300 * 256, "required observation span is preserved");
        require (audio.samples (audio.loopMs) == 96000, "loop duration is independent of block size");
        require (audio.samples (audio.recoveryMs) == 72000, "per-wrap recovery deadline is preserved");
        require (audio.recoveryDeadlineAfterWrap (95'744, 72000) == 168'000
                 && audio.recoveryDeadlineAfterWrap (94'208, 72000) == 168'000
                 && audio.recoveryDeadlineAfterWrap (190'464, 72000) == 264'000,
                 "larger blocks crossing a wrap must not extend its recovery deadline");
        const auto sharpRecovery = audio.recoverySamples (false, frames, 4096);
        require (audio.recoverySamples (true, frames, 4096) == 72000, "FREQ deadline is unchanged");
        require (sharpRecovery == (reaper ? 75776u : 72000u), "SHARP budget follows the continuous-state pipeline");
        require (sharpRecovery < audio.samples (audio.loopMs), "recovery must finish within the same lap");
        const auto deadline = audio.recoveryDeadlineAfterWrap (94'208, sharpRecovery);
        const auto lastEnd = deadline / static_cast<std::int64_t> (frames) * static_cast<std::int64_t> (frames);
        require (lastEnd <= deadline && lastEnd + static_cast<std::int64_t> (frames) > deadline,
                 "the last admitted callback ends by the deadline, never after it");
        for (const auto samples : { audio.samples (audio.linearMs), 2 * audio.samples (audio.loopMs), 12 * audio.rate })
        {
            const auto blocks = analysisBlocksForSamples (samples, frames);
            require (blocks * frames >= samples && (blocks - 1) * frames < samples,
                     "partial final blocks round up without removing audio");
        }
        const std::uint64_t threshold = reaper ? 53'333'333 : 20'000'000;
        AnalysisCallbackIntervals intervals;
        auto time = AnalysisTestClock::time_point {};
        intervals.observe (time, static_cast<int> (frames), static_cast<double> (audio.rate));
        time += nanoseconds (threshold);
        intervals.observe (time, static_cast<int> (frames), static_cast<double> (audio.rate));
        require (intervals.gaps == 0, "interval at or just below the unchanged threshold is continuous");
        time += nanoseconds (threshold + 1);
        intervals.observe (time, static_cast<int> (frames), static_cast<double> (audio.rate));
        require (intervals.gaps == 1 && intervals.maximumNanos == threshold + 1,
                 "an interval above the unchanged threshold remains a real gap");
    }
    require (analysisWaitBlocks (96000, analysisProductFrames (true)) == 563,
             "REAPER still supplies twelve audio seconds");
    const AnalysisAudioDurations reaper (true);
    require (reaper.recoverySamples (false, 256, 4096) == 72000
             && reaper.recoverySamples (false, 1024, 4096) == 72000,
             "the legacy deadline covers the smaller callback bounds");
    require (reaper.recoverySamples (false, 2048, 0) == 72000,
             "a zero-delay fixture does not receive the delayed-chain allowance");
    AnalysisPostDelay disabled;
    for (std::uint64_t i = 0; i < 10; ++i)
    {
        require (! disabled.due (i), "delay injection defaults off");
        disabled.beforePost (i, [] (auto) { require (false, "disabled jitter never waits"); });
    }
    require (disabled.injections == 0, "default runs never inject delays");
    const auto enabled = reaperAnalysisJitterDelay();
    require (! enabled.due (0) && ! enabled.due (2) && enabled.due (3) && enabled.due (6),
             "delay applies only between callbacks at the selected cadence");
}

void workConsumesThePeriodWithoutCatchUp()
{
    for (const bool reaper : { false, true })
    {
        const auto start = AnalysisTestClock::time_point {};
        const auto period = analysisProductBlockDeadline (start, reaper) - start;
        for (const auto work : { nanoseconds (0), duration_cast<nanoseconds> (period / 2),
                                duration_cast<nanoseconds> (period), duration_cast<nanoseconds> (period * 2) })
        {
            auto now = start + work;
            int sleeps = 0, yields = 0;
            const auto finish = [&] (auto began) {
                modelHostCallbackCadence (began, reaper, [&] { return now; },
                    [&] (auto deadline) {
                        require (deadline > now, "never sleep to an expired deadline");
                        now = deadline; ++sleeps;
                    }, [&] { ++yields; });
            };
            finish (start);
            require (now == start + std::max (duration_cast<nanoseconds> (period), work),
                     "start-to-start period must not add processing time to the wait");
            require (sleeps == (work < period ? 1 : 0) && yields == (work < period ? 0 : 1),
                     "exact deadline and overrun yield without a further fixed sleep");
            const auto next = now;
            finish (next);
            require (now == next + period, "an overrun never causes a catch-up burst on the next block");
        }
    }
}

hypha::live_compare::BlockClock projectBlock()
{
    hypha::live_compare::BlockClock block;
    block.frames = analysisProductFrames (true);
    block.playing = block.projectValid = true;
    block.clockAuthority = static_cast<std::uint8_t> (hypha::live_compare::ClockAuthority::certifiedProject);
    return block;
}

void reaperJitterKeepsAnAnalysisAperture()
{
    constexpr int rate = 96000, frames = analysisProductFrames (true);
    const auto blocks = analysisWaitBlocks (rate, frames);
    // AU CI elapsed times were about 4-5x local. This is a synthetic envelope, not a
    // measurement of callback cost: vary PRE preparation from 1 to 2 ms and POST/service
    // from 1 ms, scale work by 1/4/5, and add up to 5 ms to each scheduler wake-up.
    // Zero work/wake-up also pins the nominal interval. No real sleeps or shared memory.
    for (const int slowdown : { 0, 1, 4, 5 })
        for (const int wakeMs : { 0, 2, 5 })
        {
            auto now = AnalysisTestClock::time_point {};
            auto delay = reaperAnalysisJitterDelay();
            AnalysisCallbackIntervals preIntervals, postIntervals;
            hypha::live_compare::CertifiedProjectAnalysisClock preClock, postClock;
            auto pre = projectBlock(), post = projectBlock();
            post.project = -4096;
            std::uint64_t preRevision = 0, postRevision = 0, contiguous = 0;
            for (std::uint64_t callback = 0; callback < blocks; ++callback)
            {
                const auto started = now;
                now += milliseconds ((1 + callback % 2) * slowdown);
                preIntervals.observe (now, frames, rate);
                pre.afterGap = preIntervals.gaps != 0;
                const auto preReading = preClock.observe (pre, rate, true);
                delay.beforePost (callback, [&] (auto amount) { now += amount + milliseconds (wakeMs); });
                postIntervals.observe (now, frames, rate);
                post.afterGap = postIntervals.gaps != 0;
                const auto postReading = postClock.observe (post, rate, true);
                if (pre.afterGap || post.afterGap)
                {
                    std::cerr << "work scale " << slowdown << ", wake ms " << wakeMs << '\n';
                    preIntervals.print (std::cerr, "PRE"); postIntervals.print (std::cerr, "POST");
                }
                require (! pre.afterGap && ! post.afterGap, "sub-gap jitter must never manufacture a PRE or POST gap");
                require (preReading.valid && postReading.valid, "both project clocks stay valid under sub-gap jitter");
                require (callback == 0 || (preReading.revision == preRevision && postReading.revision == postRevision),
                         "jitter cannot restart either analysis aperture");
                preRevision = preReading.revision; postRevision = postReading.revision;
                contiguous += frames;
                pre.project += frames; post.project += frames;
                now += milliseconds (slowdown);
                modelHostCallbackCadence (started, true, [&] { return now; },
                    [&] (auto deadline) { now = deadline + milliseconds (wakeMs); }, [] {});
            }
            require (delay.injections == (blocks - 1) / 3, "retain every third-block delay across the full input budget");
            require (contiguous >= 12 * rate, "the entire twelve-second input budget stays continuous");
            require (now < analysisPhaseDeadline ({}, 90), "slow scheduling still finishes inside the unchanged watchdog");
            require (preIntervals.maximumNanos < 53'333'333 && postIntervals.maximumNanos < 53'333'333,
                     "synthetic load leaves headroom below the unchanged production threshold");
            if (slowdown == 0 && wakeMs == 0)
                require (postIntervals.maximumNanos >= 32'000'000 && postIntervals.maximumNanos <= 37'333'333,
                         "nominal jitter must use 60-70 percent of the gap threshold");
            std::cout << "virtual jitter work scale " << slowdown << ", wake ms " << wakeMs
                      << ", blocks " << blocks << ", injections " << delay.injections << '\n';
            preIntervals.print (std::cout, "PRE"); postIntervals.print (std::cout, "POST");
        }
}

void realGapRetiresTheApertureAndRecovers()
{
    // Gap faults have a different purpose from jitter: cross the production boundary by
    // one nanosecond, retire proof, and require fresh data. No tolerance or retry is added.
    AnalysisCallbackIntervals intervals;
    hypha::live_compare::CertifiedProjectAnalysisClock clock;
    auto block = projectBlock();
    auto now = AnalysisTestClock::time_point {};
    const auto observe = [&] {
        const auto before = intervals.gaps;
        intervals.observe (now, block.frames, 96000);
        block.afterGap = intervals.gaps != before;
        const auto reading = clock.observe (block, 96000, true);
        block.project += block.frames;
        return reading;
    };
    const auto first = observe();
    require (first.valid, "initial project proof is valid");
    now += nanoseconds (53'333'334);
    require (! observe().valid && intervals.gaps == 1, "a real gap invalidates the current aperture immediately");
    now = analysisProductBlockDeadline (now, true);
    require (! observe().valid, "the first callback after lost proof cannot reuse the old aperture");
    now = analysisProductBlockDeadline (now, true);
    const auto recovered = observe();
    require (recovered.valid && recovered.revision != first.revision && intervals.gaps == 1,
             "fresh continuous callbacks recover with a new revision");
}
}

int main()
try
{
    reaperPacingDoesNotOutrunTheExchange();
    loopRecoveryRejectsOldData();
    durationBudgetsAndGapBoundaries();
    workConsumesThePeriodWithoutCatchUp();
    reaperJitterKeepsAnAnalysisAperture();
    realGapRetiresTheApertureAndRecovers();
    std::cout << "Analysis fixture timing: 6 groups, 12 virtual jitter schedules PASS\n";
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
