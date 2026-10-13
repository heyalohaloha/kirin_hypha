#pragma once
#include "../../src/live_compare/LiveCompareOffset.h"
#include <limits>

namespace hypha::tests
{
template <typename Require>
void verifyOffsetConfidence (Require require)
{
    using namespace live_compare;
    const auto at = [] (double rate, std::int64_t lag)
    { OffsetEstimate e; e.determined = true; e.sampleRate = rate; e.lagFrames = lag;
      e.peak = 0.4; e.dominance = 20; return e; };
    int cases = 0;
    for (const double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0, 768000.0 })
    {
        OffsetMonitor monitor;
        auto e = at (rate, 0);
        monitor.observe (e, false);
        require (monitor.observe (e, false).sampleRate == rate, "settled offset retains the measurement rate for display");
        e.lagFrames = static_cast<std::int64_t> (std::ceil (rate * offsetJumpSeconds)) - 1;
        monitor.observe (e, false);
        require (! monitor.observe (e, false).jumped, "below the time threshold never holds");
        e.lagFrames = static_cast<std::int64_t> (std::ceil (rate * offsetJumpSeconds));
        // Break the previous candidate, then test the exact first report and confirmation.
        monitor.observe ({}, false);
        require (! monitor.observe (e, false).jumped, "one strong jump never holds");
        require (monitor.observe (e, false).jumped, "the time threshold catches a real delay at every rate");
        ++cases;
    }
    for (int fault = 0; fault < 6; ++fault)
    {
        OffsetMonitor monitor;
        auto e = at (96000, 0);
        monitor.observe (e, false); monitor.observe (e, false);
        e.lagFrames = 96;
        monitor.observe (e, false);
        auto weak = e;
        if (fault == 0) weak.peak = offsetJumpPeak - 0.001;
        if (fault == 1) weak.dominance = offsetJumpDominance - 0.001;
        if (fault == 2) weak.peak = std::numeric_limits<double>::quiet_NaN();
        if (fault == 3) weak.sampleRate = 0;
        if (fault == 4) weak.determined = false;
        if (fault == 5) weak.dominance = std::numeric_limits<double>::quiet_NaN();
        require (! monitor.observe (weak, false).jumped, "weak/invalid evidence never votes");
        require (! monitor.observe (e, false).jumped, "weak evidence breaks consecutive confirmation");
        require (monitor.observe (e, false).jumped, "two fresh strong reports still catch the delay");
        ++cases;
    }
    OffsetMonitor changedRate;
    changedRate.observe (at (48000, 0), false); changedRate.observe (at (48000, 0), false);
    require (! changedRate.observe (at (96000, 96), false).jumped
        && ! changedRate.observe (at (96000, 96), false).jumped, "a new rate owns a new baseline");
    std::printf ("Offset confidence: PASS %d time/quality cases + rate reset\n", cases);
}
}
