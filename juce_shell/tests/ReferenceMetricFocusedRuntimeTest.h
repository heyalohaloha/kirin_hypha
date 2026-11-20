#pragma once
// Real 48 kHz private reader -> Runtime focused Cue -> background full-cache path.
// Included after SourceScanTest's Fixture helpers; no alternate metric engine.
void verifyFocusedRuntimeTransition()
{
    constexpr std::uint32_t rate = 48000;
    const metric::Cue cue {rate * 8, rate * 9};
    Fixture fixture (rate, 2, rate * 10);
    const auto privateCopy = revision::snapshotCache().get (*fixture.source);
    require (privateCopy && privateCopy->hasPrivateCopy(), "focused runtime fixture has private audio");
    metric::Runtime runtime (fixture.root);
    const auto cache = std::make_shared<metric::SeriesCache>();
    runtime.setSeriesCache (cache);
    struct Gates
    {
        std::mutex mutex;
        std::condition_variable changed;
        unsigned entered = 0, released = 0;
        void arrive()
        {
            std::unique_lock<std::mutex> lock (mutex);
            if (entered >= 2) return;
            const auto phase = ++entered;
            changed.notify_all();
            changed.wait (lock, [&] { return released >= phase; });
        }
        bool await (unsigned phase)
        {
            std::unique_lock<std::mutex> lock (mutex);
            // Performance is tested separately. This bounded CV waits for actual
            // native work, not short sleep polling or a relaxed Job completion test.
            return changed.wait_for (lock, std::chrono::seconds (30), [&] { return entered >= phase; });
        }
        void open (unsigned phase)
        {
            { const std::lock_guard<std::mutex> lock (mutex); released = std::max (released, phase); }
            changed.notify_all();
        }
    };
    const auto gates = std::make_shared<Gates>();
    struct Unblock { std::shared_ptr<Gates> gates; ~Unblock() { gates->open (2); } } unblock {gates};
    runtime.afterEarlyForTest = [gates] { gates->arrive(); };
    runtime.service (fixture.source, rate, 2, 1, cue);
    require (gates->await (1), "focused native Cue reaches first publication gate");
    runtime.service (fixture.source, rate, 2, 1, cue);
    const auto focused = runtime.snapshot();
    require (focused && focused->state == metric::Availability::ready && focused->series
        && focused->series->boundedCue && !focused->series->fullScanComplete
        && focused->series->completeCue.start == cue.start && focused->series->completeCue.end == cue.end
        && focused->series->completeMetricMask == 7 && focused->series->sourceFrames == rate * 10
        && focused->series->frames.size() == 10 && cache->size() == 0,
        "focused Cue is ready with exact limited coverage and never enters full cache");
    runtime.service (fixture.source, rate, 2, 2, {0, rate});
    require (runtime.snapshot()->state == metric::Availability::measuring && !runtime.snapshot()->series,
             "Cue before focused coverage immediately withdraws ready facts");
    runtime.service (fixture.source, rate, 2, 3, cue);
    require (runtime.snapshot()->state == metric::Availability::ready, "return to covered Cue reuses focused facts");
    gates->open (1);
    require (gates->await (2), "uninterrupted background scan reaches the same Cue from source origin");
    runtime.service (fixture.source, rate, 2, 3, cue);
    require (runtime.snapshot()->series && !runtime.snapshot()->series->boundedCue
        && !runtime.snapshot()->series->fullScanComplete && cache->size() == 0,
        "background prefix replaces focused prefix without falsely completing full cache");
    gates->open (2); runtime.waitForTest();
    runtime.service (fixture.source, rate, 2, 3, cue);
    const auto full = runtime.snapshot();
    require (full->state == metric::Availability::ready && full->series->fullScanComplete
        && !full->series->boundedCue && full->series->frames.size() == 100 && cache->size() == 1
        && runtime.startsForTest() == 1, "one admitted job transitions to real EOF and full cache");
    for (const auto& actual : focused->series->frames)
    {
        const auto at = std::lower_bound (full->series->frames.begin(), full->series->frames.end(), actual.end_sample,
            [] (const auto& frame, std::uint64_t endpoint) { return frame.end_sample < endpoint; });
        require (at != full->series->frames.end() && at->end_sample == actual.end_sample,
                 "focused and full observations have identical absolute endpoints");
        for (std::size_t i = 0; i < 3; ++i)
        {
            const auto a = metric::value (actual, static_cast<metric::Metric> (i));
            const auto b = metric::value (*at, static_cast<metric::Metric> (i));
            require (std::isfinite (a) && std::isfinite (b) && std::abs (a - b) <= 1e-4,
                     "focused warmup and full-history real values agree within 1e-4");
        }
    }
    for (std::size_t i = 0; i < 3; ++i)
        require (focused->cue[i].complete && full->cue[i].complete
            && std::abs (focused->cue[i].median - full->cue[i].median) <= 1e-4,
            "published Cue medians also agree with full history");
    runtime.service (fixture.source, rate, 2, 4, {0, rate});
    require (runtime.snapshot()->state == metric::Availability::ready && runtime.snapshot()->series == full->series
        && runtime.startsForTest() == 1, "previously uncovered Cue uses full cache without another job");
}
