#include "LiveCompareLoopOracle.h"
#include "LiveCompareAuLoopProjectionTest.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

static thread_local bool onAudio = false;
static unsigned allocations = 0, deallocations = 0;
void* operator new (std::size_t size)
{ if (onAudio) ++allocations; if (auto* p = std::malloc (size == 0 ? 1 : size)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t size) { return ::operator new (size); }
void operator delete (void* p) noexcept { if (onAudio) ++deallocations; std::free (p); }
void operator delete[] (void* p) noexcept { ::operator delete (p); }
void operator delete (void* p, std::size_t) noexcept { ::operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { ::operator delete (p); }

using namespace loop_feasibility;
static void require (bool ok, const char* text)
{ if (! ok) { std::fprintf (stderr, "LOOP: %s\n", text); std::exit (1); } }
constexpr std::array<int, 5> buffers { 64, 128, 256, 512, 0 };
constexpr std::array<int, 8> varying { 64, 128, 512, 64, 2048, 256, 1024, 128 };

static Decision warm (Oracle& host, int delay)
{
    Decision d;
    for (int i = 0; i < (delay + 127) / 128 + 20; ++i) d = host.step (128);
    require (d.verdict == Verdict::accepted && host.tally().wrong == 0, "linear calibration must be correct");
    return d;
}

static void continuation (int length, int delay, Clock clock, Position position, int buffer, int laps)
{
    Oracle host (length, delay, clock, position, false, false);
    const auto k = warm (host, delay).k;
    host.enableLoopAtCurrentPosition();
    const auto before = host.tally();
    const auto start = host.elapsed();
    unsigned n = 0;
    Decision latest;
    while (host.elapsed() - start < static_cast<std::int64_t> (laps) * length)
    {
        onAudio = true;
        latest = host.step (buffer == 0 ? varying[n++ % varying.size()] : buffer);
        onAudio = false;
        if (position == Position::content && latest.verdict != Verdict::accepted)
            std::fprintf (stderr, "rejected length=%d delay=%d buffer=%d elapsed=%lld verdict=%d changed=%d\n",
                length, delay, buffer, static_cast<long long> (host.elapsed() - start),
                static_cast<int> (latest.verdict), latest.timelineChanged ? 1 : 0);
        require (! latest.kValid || latest.k == k, "K changed to another lap");
    }
    const auto after = host.tally();
    const auto played = after.acceptedFrames - before.acceptedFrames;
    const auto total = after.totalFrames - before.totalFrames;
    require (after.wrong == 0, "wrong position/lap reached PRE");
    if (position == Position::content || (position == Position::nativeBeforeLoop && delay < length))
        require (played == total, "proven content-coordinate loop must play every frame");
    else if (delay < length / 2)
        require (played * 100 >= total * 75, "bounded clamp must recover, not stay in POST");
    std::printf ("length=%d delay=%d clock=%d position=%d buffer=%d laps=%d PRE=%.2f%% wrong=%llu\n",
        length, delay, static_cast<int> (clock), static_cast<int> (position), buffer, laps,
        100.0 * static_cast<double> (played) / static_cast<double> (total),
        static_cast<unsigned long long> (after.wrong));
}

static void faults()
{
    using Change = void (*) (Observation&);
    const Change changes[] {
        [] (Observation& o) { o.pre.afterGap = true; },
        [] (Observation& o) { o.post.afterGap = true; },
        [] (Observation& o) { o.pre.clock -= 8192; },
        [] (Observation& o) { o.post.clock -= 8192; },
        [] (Observation& o) { o.pre.playing = false; },
        [] (Observation& o) { o.post.playing = false; },
        [] (Observation& o) { o.pre.projectValid = false; },
        [] (Observation& o) { o.post.clockValid = false; },
        [] (Observation& o) { o.pre.loop.valid = false; },
        [] (Observation& o) { o.post.loop.valid = false; },
        [] (Observation& o) { o.pre.loop.end += 1; },
        [] (Observation& o) { o.post.loop.end += 1; },
        [] (Observation& o) { o.pre.loop.bpm += 1; },
        [] (Observation& o) { o.post.loop.bpm += 1; },
        [] (Observation& o) { o.post.loop.ppq = std::numeric_limits<double>::quiet_NaN(); },
        [] (Observation& o) { o.pre.project += 9000; o.pre.loop.ppq += 9000.0 / 24000; },
        [] (Observation& o) { o.post.project -= 9000; o.post.loop.ppq -= 9000.0 / 24000; }
    };
    for (auto change : changes)
    {
        Oracle host (24000, 4096, Clock::renderCounter, Position::content, false, false);
        warm (host, 4096);
        host.enableLoopAtCurrentPosition();
        for (int n = 0; n < 400; ++n) host.step (128);
        const auto d = host.step (128, change);
        require (d.verdict != Verdict::accepted, "fault block must not output PRE");
        for (int n = 0; n < 50; ++n)
            require (host.step (128).verdict != Verdict::accepted, "lost loop proof must not reacquire a repeated position");
        require (host.tally().wrong == 0, "fault mixed unproven PRE");
    }
    std::printf ("fault cases=%zu PASS\n", sizeof (changes) / sizeof (changes[0]));
    for (auto change : { changes[4], changes[5], changes[6], changes[7] })
    {
        Oracle host (24000, 0, Clock::renderCounter, Position::content, false, false);
        warm (host, 0);
        require (host.step (128, change).verdict != Verdict::accepted, "stop/missing clock revokes proof");
        require (warm (host, 0).verdict == Verdict::accepted,
                 "non-loop stop/play must reacquire even when render clock remained continuous");
    }
    for (int delay : { 0, 24000, static_cast<int> (ringCapacityFrames) + 1 })
    {
        Oracle host (24000, delay, Clock::renderCounter, Position::content);
        for (int n = 0; n < 1000; ++n) host.step (128);
        require (host.tally().accepted == 0, "initial loop without unique K must fail closed");
    }
}

int main (int argc, char** argv)
{
    if (argc > 2 || (argc == 2 && std::strcmp (argv[1], "--matrix") != 0)) return 64;
    faults();
    verifyAuLoopProjection();
    const bool matrix = argc == 2;
    unsigned cases = 0;
    for (int length : { 24000, 48000, 96000, 192000, 384000 })
    {
        if (! matrix && length != 24000) continue;
        for (int delay : { 0, 4096, length - 1, length, length + 1 })
        for (auto clock : { Clock::vst3, Clock::renderCounter })
        for (auto position : { Position::content, Position::clamp, Position::nativeBeforeLoop })
        for (int buffer : buffers)
        {
            continuation (length, delay, clock, position, buffer, matrix ? 100 : 4);
            ++cases;
        }
    }
    std::printf ("known-K continuation: %u cases PASS; actual DAW qualification NOT RUN\n", cases);
    require (allocations == 0 && deallocations == 0, "loop RT path allocated or freed memory");
    std::printf ("RT allocations=%u frees=%u\n", allocations, deallocations);
}
