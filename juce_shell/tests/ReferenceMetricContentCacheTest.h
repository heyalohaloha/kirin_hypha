#pragma once
// Same file SHA, independent locators/identities: content reuse requires each
// locator's own verified bytes. These helpers run only with isolated temp WAVs.
void verifyAuthenticatedContentCache()
{
    Fixture original;
    const auto copy = revision::snapshotCache().get (*original.source);
    auto shared = std::make_shared<metric::SeriesCache>();
    metric::Runtime first (original.root);
    first.setSeriesCache (shared);
    first.service (original.source, 48000, 2, 1, {0, 24000}); first.waitForTest();
    first.service (original.source, 48000, 2, 1, {0, 24000});
    const auto full = first.snapshot()->series;
    require (full && first.nativeScansForTest() == 1, "original native facts measured once");
    Fixture verifiedAlias;
    verifiedAlias.source->sourceIdentityKey = "independent-verified-alias";
    require (verifiedAlias.source->sourceFileSha256 == original.source->sourceFileSha256
        && verifiedAlias.source->absolutePath != original.source->absolutePath, "same SHA at a different locator");
    const auto aliasCopy = revision::snapshotCache().get (*verifiedAlias.source);
    metric::Runtime second (verifiedAlias.root); second.setSeriesCache (shared);
    second.service (verifiedAlias.source, 48000, 2, 2, {4800, 19200});
    require (second.snapshot()->series == full && second.startsForTest() == 0 && second.nativeScansForTest() == 0,
             "verified alias shares content facts despite different identity and file revision");
    Fixture unprepared;
    unprepared.source->sourceIdentityKey = "independent-unprepared-alias";
    require (!revision::snapshotCache().prepared (*unprepared.source), "new alias starts without hash proof");
    metric::Runtime third (unprepared.root); third.setSeriesCache (shared);
    third.service (unprepared.source, 48000, 2, 3, {0, 24000});
    require (third.snapshot()->state == metric::Availability::measuring && !third.snapshot()->series,
             "unverified locator cannot immediately borrow ready SHA facts");
    third.waitForTest(); third.service (unprepared.source, 48000, 2, 3, {0, 24000});
    require (third.snapshot()->series == full && third.startsForTest() == 1 && third.nativeScansForTest() == 0,
             "worker authenticates new locator then rechecks cache without native rescan");
    third.service (unprepared.source, 48000, 2, 4, {4800, 14400});
    require (third.snapshot()->series == full && third.snapshot()->selectionGeneration == 4
        && third.startsForTest() == 1, "accepted locator proof permits later Cue reuse without repeated copy jobs");
    Fixture forged (48000, 2, 28800);
    require (forged.source->sourceFileSha256 != original.source->sourceFileSha256, "negative file really has different bytes");
    forged.source->sourceFileSha256 = original.source->sourceFileSha256;
    forged.source->sourceIdentityKey = "false-content-claim";
    metric::Runtime rejected (forged.root); rejected.setSeriesCache (shared);
    rejected.service (forged.source, 48000, 2, 5, {0, 24000});
    require (!rejected.snapshot()->series, "false SHA is not accepted before verification");
    if (rejected.startsForTest() != 0) rejected.waitForTest();
    rejected.service (forged.source, 48000, 2, 5, {0, 24000});
    require (rejected.snapshot()->state != metric::Availability::ready && !rejected.snapshot()->series
        && rejected.nativeScansForTest() == 0, "actual differing bytes reject forged SHA without native measurement or cache reuse");
    auto mismatched = std::make_shared<ref::RuntimeSource> (*verifiedAlias.source);
    mismatched->revision.modifiedTimeNs += "1";
    metric::Runtime stale (verifiedAlias.root); stale.setSeriesCache (shared);
    stale.service (mismatched, 48000, 2, 6, {0, 24000});
    if (stale.startsForTest() != 0) stale.waitForTest();
    stale.service (mismatched, 48000, 2, 6, {0, 24000});
    require (!stale.snapshot()->series && stale.nativeScansForTest() == 0,
             "snapshot/source revision mismatch never authorizes content reuse");
    require (shared->size() == 1, "verified aliases count as one song; forged or stale sources add no cache entry");
}

void verifyContentCacheFormatSeparation()
{
    Fixture stereo;
    const auto copy = revision::snapshotCache().get (*stereo.source);
    const auto shared = std::make_shared<metric::SeriesCache>();
    metric::Runtime at48 (stereo.root); at48.setSeriesCache (shared);
    at48.service (stereo.source, 48000, 2, 1, {0, 24000}); at48.waitForTest();
    at48.service (stereo.source, 48000, 2, 1, {0, 24000});
    const auto full48 = at48.snapshot()->series;
    require (full48 && full48->rate == 48000, "format fixture completes at 48 kHz");
    metric::Runtime at44 (stereo.root); at44.setSeriesCache (shared);
    at44.service (stereo.source, 44100, 2, 2, {0, 22050});
    require (at44.snapshot()->state == metric::Availability::measuring && !at44.snapshot()->series,
             "different host rate cannot immediately reuse 48 kHz facts");
    at44.waitForTest(); at44.service (stereo.source, 44100, 2, 2, {0, 22050});
    const auto full44 = at44.snapshot()->series;
    require (full44 && full44 != full48 && full44->rate == 44100 && full44->sourceFrames == 22050
        && at44.nativeScansForTest() == 1 && shared->size() == 2,
        "same SHA at 44.1 kHz requires one native scan and a separate content entry");
    at44.service (stereo.source, 44100, 2, 3, {4410, 17640});
    require (at44.snapshot()->series == full44 && at44.nativeScansForTest() == 1,
             "new-rate Cue reuses only the new-rate series");

    // Existing AudioPages contract permits mono -> duplicated stereo, not stereo
    // -> mono downmix. Exercise the permitted conversion without changing policy.
    Fixture mono (48000, 1, 24000);
    const auto monoCopy = revision::snapshotCache().get (*mono.source);
    const auto channelCache = std::make_shared<metric::SeriesCache>();
    metric::Runtime one (mono.root); one.setSeriesCache (channelCache);
    one.service (mono.source, 48000, 1, 1, {0, 24000}); one.waitForTest();
    one.service (mono.source, 48000, 1, 1, {0, 24000});
    const auto monoFacts = one.snapshot()->series;
    metric::Runtime two (mono.root); two.setSeriesCache (channelCache);
    two.service (mono.source, 48000, 2, 2, {0, 24000});
    require (!two.snapshot()->series, "different host channels cannot borrow mono facts");
    two.waitForTest(); two.service (mono.source, 48000, 2, 2, {0, 24000});
    require (monoFacts && two.snapshot()->series && two.snapshot()->series != monoFacts
        && monoFacts->channels == 1 && two.snapshot()->series->channels == 2
        && two.nativeScansForTest() == 1 && channelCache->size() == 2,
        "same mono SHA at two supported channel layouts has separate native series");

    // The implementation exposes one definition only. Test its cache namespace,
    // not a fictional second algorithm: wrong-version/gain entries must be misses.
    const auto definitions = std::make_shared<metric::SeriesCache>();
    const auto stem = stereo.source->sourceFileSha256 + "\n48000:2:";
    require (definitions->insert (stem + "native-metrics-v0:gain1", full48)
        && definitions->insert (stem + "native-metrics-v1:gain2", full48), "seed incompatible definition namespaces");
    metric::Runtime current (stereo.root); current.setSeriesCache (definitions);
    current.service (stereo.source, 48000, 2, 4, {0, 24000});
    require (!current.snapshot()->series, "incompatible definition or gain cannot supply ready facts");
    current.waitForTest(); current.service (stereo.source, 48000, 2, 4, {0, 24000});
    require (current.snapshot()->series && current.snapshot()->series != full48
        && current.nativeScansForTest() == 1 && definitions->size() == 3,
        "current definition/gain produces its own entry after one native scan");
}
