#pragma once
#include "../../src/live_compare/LiveCompareClock.h"

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
}
