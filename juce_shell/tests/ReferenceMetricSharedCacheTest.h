#pragma once
// Included by SourceScanTest after its isolated Fixture helpers. No extra reader,
// benchmark, or host is started: this checks the same two worker-facing Runtimes.
void verifySharedRoleCache()
{
    Fixture a;
    Fixture b (48000, 2, 28800);
    const auto aCopy = revision::snapshotCache().get (*a.source);
    const auto bCopy = revision::snapshotCache().get (*b.source);
    auto shared = std::make_shared<metric::SeriesCache>();
    metric::Runtime reference (a.root), check (a.root);
    reference.setSeriesCache (shared); check.setSeriesCache (shared);
    reference.service (a.source, 48000, 2, 1, {0, 24000}); reference.waitForTest();
    reference.service (a.source, 48000, 2, 1, {0, 24000});
    const auto original = reference.snapshot()->series;
    require (original && reference.startsForTest() == 1, "B role completes A once");
    reference.clear(); // Losing priority withdraws the role, not shared completed facts.
    check.service (a.source, 48000, 2, 2, {4800, 19200});
    require (check.snapshot()->series == original && check.startsForTest() == 0,
             "C role reuses B's completed same-source series with no second scan");
    check.service (b.source, 48000, 2, 3, {0, 24000}); check.waitForTest();
    check.service (b.source, 48000, 2, 3, {0, 24000});
    require (check.snapshot()->series && shared->size() == 2, "B/C contribute to one aggregate cache");
    check.clear(); reference.service (b.source, 48000, 2, 4, {0, 24000});
    require (reference.startsForTest() == 1 && reference.snapshot()->series,
             "B also reuses C's completed series without scanning again");
    // Synthetic full observations fill the same shared object, independent of PCM.
    for (int i = 0; i < 7; ++i)
    {
        auto value = std::make_shared<metric::SeriesData>();
        value->fullScanComplete = true; value->frames.resize (1);
        require (shared->insert ("extra-" + juce::String (i), value), "fill shared role cache");
    }
    require (shared->size() == 8 && shared->bytes() <= metric::SeriesCache::maximumBytes,
             "both roles together retain at most eight series within one byte budget");
    // Evicting A cannot invalidate immutable data already held by a display consumer.
    require (original->frames.size() == 5, "evicted display snapshot retains its immutable lifetime");
}
