#pragma once
#include "reference_audition/ReferenceMetricRuntime.h"
#include <stdexcept>
inline void verifyMetricCueCoverage()
{
    namespace metrics = hypha::reference_audition::metrics;
    const auto check = [] (bool ok) { if (!ok) throw std::runtime_error ("bounded Cue coverage violated"); };
    for (const auto rate : {44100u, 48000u, 96000u}) check (metrics::Runtime::warmupMaskForTest (rate) == 7);
    for (const auto rate : {8000u, 32000u, 88200u, 192000u}) check (metrics::Runtime::warmupMaskForTest (rate) == 0);
    auto data = std::make_shared<metrics::SeriesData>();
    data->sourceFrames = 48000 * 180; data->rate = 48000; data->channels = 2;
    data->boundedCue = true; data->completeCue = {48000 * 120 + 137, 48000 * 140 + 231};
    data->completeMetricMask = 3;
    // The last retained 100ms endpoint is before a nonaligned Cue end. The
    // explicit complete range, not the last frame, proves that fractional tail.
    KirinReferenceMetricFrame frame {}; frame.end_sample = 48000 * 140;
    data->frames.push_back (frame);
    check (metrics::Runtime::coversForTest (data, data->completeCue));
    check (metrics::Runtime::coversForTest (data, {48000 * 125, 48000 * 130}));
    check (!metrics::Runtime::coversForTest (data, {48000 * 120, 48000 * 130}));
    check (!metrics::Runtime::coversForTest (data, {48000 * 125, 48000 * 141}));
    check (!metrics::Runtime::coversForTest (data, {48000 * 125, 48000 * 125}));
    metrics::SeriesCache cache;
    check (!cache.insert ("bounded", data));
    data->boundedCue = false;
    check (!metrics::Runtime::coversForTest (data, {48000 * 125, 48000 * 140 + 231}));
    data->fullScanComplete = true;
    check (metrics::Runtime::coversForTest (data, {48000 * 125, 48000 * 140 + 231}));
}
