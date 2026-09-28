#include "LiveCompareHostModel.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>

// The model's Audio Thread work (publish and process) must not allocate.
static thread_local bool inRt = false;
static std::atomic<unsigned> rtAllocations { 0 };
void* operator new (std::size_t bytes)
{
    if (inRt) ++rtAllocations;
    if (auto* p = std::malloc (bytes == 0 ? 1 : bytes)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t bytes) { return ::operator new (bytes); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { ::operator delete (p); }
void operator delete (void* p, std::size_t) noexcept { ::operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { ::operator delete (p); }

using namespace live_compare_test;

static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "FAIL: " << message << '\n'; std::abort(); }
}

static void print (const char* name, const Tally& t)
{
    std::printf ("%s: evaluated %d, accepted %d (false %d), calibrating %d, before run %d, not written %d, "
                 "overwritten %d, writing/torn %d, gaps PRE %d POST %d, M1 invalidations %d\n",
                 name, t.evaluated, t.accepted, t.falseAccepted, t.calibrating, t.beforeRun, t.notWritten,
                 t.overwritten, t.writingOrTorn, t.gapsPre, t.gapsPost, t.disagreementInvalidations);
}

// The G1 protocol: loops, seeks, stop and play, a backward seek within one pass, a forward seek into
// silence, a PRE-only sleep during playback and a POST-only sleep of about 5 s during a stop.
static Tally protocol (HostModel model, CalibrationProfile calibration = {})
{
    Host host (model, calibration);
    inRt = true;
    host.loopOn (0, 390000);
    host.play (0);
    host.run (420);
    host.seek (140640);
    host.run (150);
    host.stop();
    host.run (50);
    host.play (host.located());
    host.run (200);
    host.stop();
    host.run (20);
    host.loopOff();
    host.play (0);
    host.run (120);
    host.seek (96000);
    host.run (120);
    host.sleepPre (140);
    host.run (300);
    host.stop();
    host.run (100);
    host.play (0);
    host.run (200);
    host.stop();
    host.sleepPost (120);
    host.run (211);
    host.play (0);
    host.run (200);
    host.stop();
    host.run (20);
    inRt = false;
    return host.tally();
}

// INV-LC1, INV-LC2, INV-LC3: no false acceptance under the measured host behaviours.
static void measuredHostsNeverAcceptWrongPre()
{
    const struct { const char* name; HostModel model; } cases[] = {
        { "Studio Pro VST3, delay 4096", { Format::vst3, LoopReport::clampToLoopStart, 4096, false } },
        { "Studio Pro VST3, adjacent", { Format::vst3, LoopReport::clampToLoopStart, 0, false } },
        { "Studio Pro AU, delay 4096", { Format::audioUnit, LoopReport::clampToLoopStart, 4096, false } },
        { "Studio Pro AU, adjacent", { Format::audioUnit, LoopReport::clampToLoopStart, 0, false } },
        { "Pro Tools AAX, delay 4096", { Format::aax, LoopReport::followContent, 4096, false } },
        { "Pro Tools AAX, adjacent", { Format::aax, LoopReport::followContent, 0, false } },
    };
    for (const auto& c : cases)
    {
        const auto t = protocol (c.model);
        print (c.name, t);
        require (t.falseAccepted == 0, "a measured host behaviour produced a false acceptance");
        require (t.evaluated > 1500 && t.accepted > 1000, "too few blocks evaluated or accepted");
        require (t.gapsPre == 1 && t.gapsPost == 1, "exactly the modelled PRE and POST sleeps are gaps");
        require (t.writingOrTorn == 0, "a sequential host never tears a read");
    }
    require (rtAllocations.load() == 0, "publish or process allocated on the Audio Thread");
}

// The control for the gap rule: without it, an AU POST-only sleep inside the ring capacity maps
// POST onto stale PRE audio. The product rule rejects exactly that.
static void postOnlySleepIsCaughtByTheGapRule()
{
    const auto t = protocol ({ Format::audioUnit, LoopReport::clampToLoopStart, 4096, false });
    require (t.falseAccepted == 0 && t.gapsPost == 1, "the POST-only sleep was not neutralised");
}

// INV-LC3 and INV-LC5: a latency change while playing. The audio changes first; the host
// re-compensates POST's clocks later. M1 bounds the undetected window to the host lag plus one block.
static Tally latencySwitch (HostModel model, CalibrationProfile calibration, int lagDown, int lagUp)
{
    Host host (model, calibration);
    host.loopOn (0, 390000);
    host.play (0);
    host.run (200);
    host.setLatency (0, lagDown);
    host.run (200);
    host.setLatency (4096, lagUp);
    host.run (200);
    host.stop();
    host.run (10);
    return host.tally();
}

static void latencyChangesAreBoundedByM1()
{
    CalibrationProfile withM1;
    CalibrationProfile withoutM1;
    withoutM1.invalidateOnDisagreement = false;
    const HostModel aax { Format::aax, LoopReport::followContent, 4096, false };
    const auto m1 = latencySwitch (aax, withM1, 1, 0);          // Pro Tools 2026.4: 1 block down, same block up
    const auto noM1 = latencySwitch (aax, withoutM1, 1, 0);
    print ("Pro Tools AAX latency switch, M1", m1);
    print ("Pro Tools AAX latency switch, no M1", noM1);
    require (m1.falseAccepted <= 3, "M1 must bound the AAX latency-change window to the host lag");
    require (noM1.falseAccepted > m1.falseAccepted, "the control must show what M1 removes");
    require (m1.disagreementInvalidations == 2, "M1 invalidates K once per latency change only");

    const HostModel vst3 { Format::vst3, LoopReport::clampToLoopStart, 4096, false };
    const auto studio = latencySwitch (vst3, withM1, 2, 3); // Studio Pro 8.1.2: 2 and 3 blocks
    print ("Studio Pro VST3 latency switch", studio);
    require (studio.falseAccepted <= 5, "the VST3 window stays within the host re-compensation lag");
}

// Documented limit (G1 record 8.6, plan 5.2): a host that labels the stale audio after a relocation
// inside PRE's new run cannot be told apart by clocks. Host certification checks content.
static void mislabellingHostIsAKnownLimit()
{
    const auto t = protocol ({ Format::vst3, LoopReport::clampToLoopStart, 4096, true });
    print ("hypothetical mislabelling host", t);
    require (t.falseAccepted > 0, "the clock rules are not expected to see a mislabelling host");
}

// POST refuses a ring stamped for another pair or sample rate and copies nothing.
static void foreignRingIsRefused()
{
    auto ring = std::make_unique<Ring>();
    ring->initialise (pairKey + 1, 48000);
    Consumer consumer;
    std::vector<float> a (blockFrames, 7.0f), b (blockFrames, 7.0f);
    float* out[] = { a.data(), b.data() };
    BlockClock block;
    block.clock = 0; block.project = 0; block.frames = blockFrames;
    block.clockValid = block.projectValid = block.playing = true;
    require (consumer.process (*ring, pairKey, 48000, block, out, 2).verdict == Verdict::foreignRing,
             "a ring for another pair must be refused");
    ring->initialise (pairKey, 48000);
    require (consumer.process (*ring, pairKey, 44100, block, out, 2).verdict == Verdict::foreignRing,
             "a ring for another sample rate must be refused");
    require (a[0] == 7.0f && b[0] == 7.0f, "a refused ring must not write the scratch output");
}

// INV-LC2: gap detection uses the host-profile factor and floor.
static void gapThresholdsFollowTheProfile()
{
    GapDetector gaps;
    require (! gaps.observe (1000000000ull, 2048, 48000.0), "the first call is never a gap");
    require (! gaps.observe (1000000000ull + 100000000ull, 2048, 48000.0), "100 ms after a 42.7 ms block is not a gap");
    require (gaps.observe (1000000000ull + 100000000ull + 120000000ull, 64, 48000.0), "120 ms after a 42.7 ms block is a gap");
    require (! gaps.observe (1000000000ull + 220000000ull + 15000000ull, 64, 48000.0), "the 20 ms floor holds for small blocks");
    GapDetector strict ({ 1.5, 0.005 });
    strict.observe (0, 1024, 48000.0);
    require (strict.observe (40000000ull, 1024, 48000.0), "a stricter profile flags 40 ms after 21.3 ms");
}

// Concurrent PRE and POST (hosts that process the two instances on different threads): every block
// POST accepts holds exactly the PRE samples of its clock range, never a mix of two writes.
static void concurrentReadersNeverSeeTornBlocks()
{
    auto ring = std::make_unique<Ring>();
    ring->initialise (pairKey, 48000);
    std::atomic<bool> done { false };
    std::atomic<std::int64_t> writerClock { 0 };
    std::thread writer ([&]
    {
        Publisher publisher;
        std::vector<float> left (256), right (256);
        for (std::int64_t clock = 0; clock < 256 * 20000; clock += 256)
        {
            for (int i = 0; i < 256; ++i) { left[size_t (i)] = content (clock + i, 0); right[size_t (i)] = content (clock + i, 1); }
            const float* channels[] = { left.data(), right.data() };
            BlockClock b;
            b.clock = b.project = clock; b.frames = 256;
            b.clockValid = b.projectValid = b.playing = true;
            publisher.publish (*ring, b, channels, 2);
            writerClock.store (clock + 256, std::memory_order_release);
        }
        done.store (true, std::memory_order_release);
    });
    Consumer consumer ({ 2, true });
    std::vector<float> left (256), right (256);
    float* out[] = { left.data(), right.data() };
    int accepted = 0;
    std::int64_t next = -1;
    while (! done.load (std::memory_order_acquire))
    {
        const auto end = writerClock.load (std::memory_order_acquire);
        if (end < 1024 || (next >= 0 && next + 256 > end))
            continue; // wait until the next contiguous block is written
        if (next < 0 || end - next > 4096)
        {
            next = end - 512; // restart close to the writer after falling behind
            consumer.reset();
        }
        BlockClock b;
        b.clock = b.project = next; b.frames = 256;
        b.clockValid = b.projectValid = b.playing = true;
        const auto d = consumer.process (*ring, pairKey, 48000, b, out, 2);
        next += 256;
        if (d.verdict != Verdict::accepted) continue;
        ++accepted;
        for (int i = 0; i < 256; ++i)
            require (left[size_t (i)] == content (d.preStart + i, 0) && right[size_t (i)] == content (d.preStart + i, 1),
                     "an accepted block mixed two PRE writes");
    }
    writer.join();
    std::printf ("concurrent reader: %d blocks accepted and verified\n", accepted);
}

int main()
{
    measuredHostsNeverAcceptWrongPre();
    postOnlySleepIsCaughtByTheGapRule();
    latencyChangesAreBoundedByM1();
    mislabellingHostIsAKnownLimit();
    foreignRingIsRefused();
    gapThresholdsFollowTheProfile();
    concurrentReadersNeverSeeTornBlocks();
    std::printf ("live compare correspondence: all checks passed\n");
    return 0;
}
