#pragma once
#include "../../src/live_compare/LiveCompareClock.h"
#include "../../src/live_compare/LiveCompareLoopEntry.h"

static void rawClockOriginsNeverSilentlyShareAProof()
{
    using namespace hypha::live_compare;
    using hypha::AuxiliaryClockSource;
    static_assert (sizeof (ContinuousClock) <= 16, "clock provenance stays fixed-size per instance");
    ContinuousClock clock;
    auto reading = clock.next ({ 10000, AuxiliaryClockSource::vst3Continuous, true }, 128);
    require (reading.valid && reading.samples == 10000 && reading.basis == ClockBasis::vst3Continuous,
             "VST3 raw origin stays explicit");
    reading = clock.next ({ 10128, AuxiliaryClockSource::audioUnitRender, true }, 128);
    require (! reading.valid && reading.basis == ClockBasis::audioUnitRender,
             "numerically continuous but different AU origin revokes the old proof");
    reading = clock.next ({ 10256, AuxiliaryClockSource::audioUnitRender, true }, 128);
    require (reading.valid && reading.samples == 10256, "AU reacquires through the ordinary correspondence gate");
    require (! clock.next ({ 10384, AuxiliaryClockSource::audioUnitRender, false }, 128).valid,
             "a missing AU value is not silently replaced by a valid counter");
    reading = clock.next ({ 10384, AuxiliaryClockSource::aaxEngine, true }, 128);
    require (! reading.valid && reading.basis == ClockBasis::aaxEngine && reading.samples == 10384,
             "changing to the subscribed DAE clock fences the previous origin");
    reading = clock.next ({ 10512, AuxiliaryClockSource::aaxEngine, true }, 128);
    require (reading.valid && reading.samples == 10512,
             "the qualified AAX clock remains host-owned rather than a private counter");
    require (! callbackGapBreaksContinuity (true, ClockBasis::vst3Continuous)
        && ! callbackGapBreaksContinuity (true, ClockBasis::audioUnitRender)
        && ! callbackGapBreaksContinuity (true, ClockBasis::aaxEngine)
        && callbackGapBreaksContinuity (true, ClockBasis::pluginFrames)
        && ! callbackGapBreaksContinuity (false, ClockBasis::pluginFrames),
        "wall delay only fences an unqualified private counter");
    require (! clock.next ({ 256, AuxiliaryClockSource::vst3Continuous, true }, 128).valid,
             "a new VST3 origin also fences the counter proof");
    require (! clock.next ({ 384, AuxiliaryClockSource::vst3Continuous, true }, 0).valid,
             "empty callbacks are not timing evidence");
    require (clock.next ({ 384, AuxiliaryClockSource::vst3Continuous, true }, 128).valid,
             "an empty callback does not replace the current origin");

    BlockClock post;
    post.clock = 50000; post.project = 2000; post.frames = 128;
    post.clockValid = post.projectValid = post.playing = post.outputPresentationValid = true;
    post.clockBasis = static_cast<std::uint8_t> (ClockBasis::vst3Continuous);
    post.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::cubaseCycleContent);
    post.presentationSource = 1; post.outputPresentationSamples = 256;
    post.loop = { true, true, 2000.0 / 24000.0, 0.0, 1.0, 120.0 };
    auto pre = post; pre.clock += 4096; pre.project += 4096;
    pre.loop.ppq = 6096.0 / 24000.0; pre.outputPresentationSamples += 4096;
    LoopAnchor anchor;
    anchor.loopKnown = true; anchor.loopSamples = 24000;
    anchor.loop = { true, true, 0.0, 0.0, 1.0, 120.0 };
    require (loopAddressIsCurrent (anchor, pre, post, 0, 48000, 1048576),
             "Cubase K=0 needs both the content anchor and current presentation distance");
    auto delayed = pre; delayed.clock += 24000; delayed.outputPresentationSamples += 24000;
    require (loopAddressIsCurrent (anchor, delayed, post, 0, 48000, 1048576),
             "a genuine whole-lap PDC remains distinguishable by its continuous clock and presentation report");
    for (const auto obsolete : { -24000, 4096, 24000 })
        require (!loopAddressIsCurrent (anchor, pre, post, obsolete, 48000, 1048576),
                 "an obsolete linear K or another lap cannot override Cubase's content origin");
    for (const auto offset : { -24000, -96, 96, 24000 })
    {
        auto shifted = post; shifted.clock += offset;
        require (!loopAddressIsCurrent (anchor, pre, shifted, 0, 48000, 1048576),
                 "a PDC or whole-lap clock contradiction cannot be excused by repeated positions");
    }
    auto verified = post;
    require (!corroborateLoop (anchor, post, post.clock, 48000, verified),
             "the PCM anchor alone cannot bypass the current Cubase pair witness");
}
