#include "ProductTestClock.h"
#include "../src/live_compare/LiveCompareClock.h"
#include "../src/live_compare/CertifiedProjectAnalysisClock.h"
#include <array>
#include <iostream>
#include <thread>

using namespace hypha::live_compare;
using namespace std::chrono;
static void require (bool ok, const char* why)
{ if (! ok) throw std::runtime_error (why); }

// Heap-local production clock policy. No named shared memory, DAW, JUCE or GUI is needed.
static std::array<bool, 8> run (int frames, double rate, int realDelayMs)
{
    hypha::tests::ProductTestClock time;
    GapDetector gaps;
    CertifiedProjectAnalysisClock analysis;
    std::array<bool, 8> result {};
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        std::this_thread::sleep_for (milliseconds (realDelayMs));
        BlockClock block;
        block.frames = frames; block.playing = block.projectValid = true;
        block.project = static_cast<std::int64_t> (i) * frames;
        block.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::certifiedProject);
        block.wallNanos = callbackWallNanos();
        block.afterGap = gaps.observe (block.wallNanos, frames, rate);
        result[i] = analysis.observe (block, rate, true).valid;
        require (! block.afterGap, "real scheduler delay must not become a host gap");
        time.advanceBlock (frames, rate);
    }
    time.advance (milliseconds (600));
    require (gaps.observe (callbackWallNanos(), frames, rate), "explicit stall must remain a gap");
    time.advanceBlock (frames, rate);
    require (! gaps.observe (callbackWallNanos(), frames, rate), "regular cadence recovers after a gap");
    return result;
}

int main()
try
{
    for (auto format : { std::pair<int, double> { 256, 48000 }, { 2048, 96000 } })
    {
        const auto baseline = run (format.first, format.second, 0);
        for (const auto valid : baseline) require (valid, "ordinary analysis input is valid");
        for (const auto delay : { 40, 80, 175 })
            require (baseline == run (format.first, format.second, delay), "slow machine has identical analysis results");
    }
    {
        hypha::tests::ProductTestClock time;
        GapDetector gap;
        gap.observe (time.nanos(), 256, 48000);
        time.advance (milliseconds (20));
        require (! gap.observe (time.nanos(), 256, 48000), "threshold equality is not a gap");
        time.advance (nanoseconds (20'000'001));
        require (gap.observe (time.nanos(), 256, 48000), "one ns over the threshold is a gap");
        const auto start = time.nanos();
        for (int i = 0; i < 44100; ++i) time.advanceBlock (1, 44100);
        require (time.nanos() - start >= 999'999'999 && time.nanos() - start <= 1'000'000'000,
                 "fractional sample periods do not drift");
    }
    require (testCallbackNanos.load() == 0, "clock owner restores real clock on scope exit");
    std::cout << "callback clock: fast/40/80/175 ms identical, explicit stall, recovery, boundary, fractional rate PASS\n";
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
