#pragma once
#include "ReferenceManualService.h"
#include "../src/reference_audition/ReferenceSourceSnapshot.h"
#include <future>

inline juce::WaitableEvent* snapshotCopyEntered = nullptr;
inline juce::WaitableEvent* snapshotCopyReleased = nullptr;
inline void holdSnapshotCopy()
{
    snapshotCopyEntered->signal();
    snapshotCopyReleased->wait (-1);
}
inline std::atomic<int> monitorHashReads { 0 };
inline void finishSlowSourceHash()
{
    ++monitorHashReads;
    hypha::reference_audition::revision_detail::monitorClockOffsetMsForTest.fetch_add (31000);
}
inline void countMonitorHash() { ++monitorHashReads; }
inline bool readMonitorRevision (const juce::File& file, ref::RuntimeFileRevision& revision)
{
    ref::RuntimeSource source; source.absolutePath = file.getFullPathName();
    return ref::RuntimeV2SourceRepository (juce::File {}).readSourceRevision (source, revision);
}

inline juce::WaitableEvent shutdownCopyEntered, shutdownCopyRelease;
inline std::shared_ptr<ref::revision_detail::PreparedSourceAudio> shutdownPreparedAudio;
inline void holdShutdownCopy() { shutdownCopyEntered.signal(); shutdownCopyRelease.wait (-1); }
inline void releaseShutdownCopy() { shutdownCopyRelease.signal(); }
inline void verifyShutdownCache()
{
    // This runs from the real singleton cache destructor, after the service joins.
    if (!shutdownPreparedAudio || !shutdownPreparedAudio->snapshot || !shutdownPreparedAudio->snapshot->available()) std::abort();
    shutdownPreparedAudio.reset();
    std::cout << "joined-before-cache" << std::flush;
}
inline bool runSourceShutdownChild (int argc, char** argv)
{
    if (argc != 3 || juce::String (argv[1]) != "--source-shutdown-child") return false;
    namespace revision = ref::revision_detail;
    const juce::File file { juce::String (argv[2]) };
    ref::RuntimeSource source; source.absolutePath = file.getFullPathName();
    source.sourceFileSha256 = juce::SHA256 (file).toHexString();
    require (readMonitorRevision (file, source.revision), "shutdown fixture revision");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    require (reader != nullptr, "shutdown fixture is actual audio");
    source.audio.sampleRateHz = static_cast<int> (reader->sampleRate);
    source.audio.channels = reader->numChannels; source.audio.totalSampleFrames = reader->lengthInSamples;
    auto current = source.revision; source.revision.modifiedTimeNs = "0";
    shutdownPreparedAudio = source.preparedAudio;
    auto& service = revision::service();
    revision::sourceServiceStoppingHookForTest.store (&releaseShutdownCopy);
    revision::snapshotCacheDestroyedHookForTest.store (&verifyShutdownCache);
    revision::snapshotCopyHookForTest.store (&holdShutdownCopy);
    require (service.check (source, current, &readMonitorRevision).state == ref::SourceRevisionState::revalidate,
             "changed metadata queues the real service worker");
    require (shutdownCopyEntered.wait (30000), "worker owns an in-flight copy when main returns normally");
    return true;
}
inline void testReferenceSourceShutdown (const juce::File& source)
{
    juce::ChildProcess child;
    require (child.start (juce::StringArray { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
        "--source-shutdown-child", source.getFullPathName() }), "start normal-exit fixture child");
    require (child.waitForProcessToFinish (30000), "normal exit joins an in-flight verification without deadlock");
    require (child.getExitCode() == 0 && child.readAllProcessOutput().contains ("joined-before-cache"),
             "normal singleton teardown finishes and publishes the copy before destroying its cache");
}

inline void testReferenceSnapshotStorage (const juce::File& root, const ref::RuntimeSource& original)
{
    namespace revision = ref::revision_detail;
    auto source = original;
    const auto file = root.getChildFile ("copy-storage.wav");
    require (juce::File (source.absolutePath).copyFileTo (file), "isolated snapshot storage fixture");
    source.absolutePath = file.getFullPathName();
    require (readMonitorRevision (file, source.revision), "snapshot storage fixture revision");
    struct Reset { ~Reset() {
        revision::snapshotFreeBytesForTest.store (-1); revision::snapshotWriteLimitForTest.store (-1);
    } } reset;
    const auto baseline = revision::SnapshotStorage::reservedForTest();
    const auto length = source.revision.sizeBytes.getLargeIntValue();
    revision::snapshotFreeBytesForTest.store (revision::SnapshotStorage::headroom + baseline + length + length / 2);
    {
        revision::SourceSnapshot first (source);
        require (first.available(), "one full snapshot fits while retaining disk headroom");
        const auto reads = revision::snapshotReadBytesForTest.load();
        revision::SourceSnapshot second (source);
        require (second.available() && !second.hasPrivateCopy() && second.storageNotice() == "reference_source_cache_full"
            && revision::snapshotReadBytesForTest.load() - reads == length && !second.fileForTest().exists(),
            "live copy totals preserve disk headroom while the second source remains usable");
        require (revision::SnapshotStorage::reservedForTest() == baseline + length,
                 "only the successful snapshot owns disk bytes");
    }
    require (revision::SnapshotStorage::reservedForTest() == baseline, "retiring a snapshot releases its exact storage budget");
    revision::snapshotFreeBytesForTest.store (-1);
    revision::snapshotWriteLimitForTest.store (64 * 1024 + 128);
    const auto reads = revision::snapshotReadBytesForTest.load();
    revision::SourceSnapshot partial (source);
    require (partial.available() && !partial.hasPrivateCopy() && partial.storageNotice() == "reference_source_copy_failed"
        && revision::snapshotReadBytesForTest.load() - reads == length
        && !partial.fileForTest().exists() && revision::SnapshotStorage::reservedForTest() == baseline,
        "a failed destination removes its partial file and finishes verification for authenticated direct reading");
    revision::snapshotWriteLimitForTest.store (-1);
    revision::SourceSnapshot recovered (source);
    require (recovered.available(), "the exact source copies successfully after temporary storage recovers");
}

inline void testReferenceAsyncSourcePreparation (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    namespace revision = ref::revision_detail;
    const auto root = sandbox.getChildFile ("async-source-handoff");
    require (root.createDirectory().wasOk(), "isolated asynchronous preparation fixture");
    publishTwoCheckLibrary (root, 2, true);
    ref::ReferenceComparisonController comparison (root);
    control::stopWorkers (comparison);
    comparison.configure ({"async-source-post", {}, 42, true}, 48000, 2);
    comparison.setPresented (true); control::service (comparison);
    comparison.observeTransport (0, true, true); control::service (comparison);
    auto& runtime = control::roleFor (comparison, {2});
    require (comparison.requestAudition (2, -22, -6), "select the prepared C before asynchronous replacement");
    juce::AudioBuffer<float> block (2, 4096);
    std::int64_t cursor = 0;
    const auto feed = [&] {
        for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (block.getWritePointer (ch), 0.125f, 4096);
        comparison.observeTransport (cursor, true, true);
        comparison.observeAInput (block, cursor, true, true, true);
        require (comparison.renderSelectedB (block, cursor, true, true, true), "verified C stays audible during preparation");
        cursor += 4096; control::service (comparison);
    };
    feed();
    control::stagedLaunchFor (runtime, {}) = std::launch::async;
    juce::WaitableEvent entered, release;
    snapshotCopyEntered = &entered; snapshotCopyReleased = &release;
    const auto copies = revision::snapshotCopiesForTest.load();
    revision::snapshotCopyHookForTest.store (&holdSnapshotCopy);
    const auto state = comparison.snapshot();
    require (comparison.selectCheck (state.checkTargets[1].id), "request a different C through the real entry");
    control::service (comparison);
    const bool copying = entered.wait (30000);
    // Continue real callbacks while the separate source worker is held.
    // No short sleeps or fabricated capture/publication readiness.
    bool quietNotice = true;
    if (copying) for (int n = 0; n < 64; ++n)
    {
        control::elapsePreparationClocks (runtime, {}); feed();
        quietNotice = quietNotice && comparison.snapshot().checkSelection->pendingSourceFailure.isEmpty();
    }
    release.signal();
    require (copying && control::waitForSourceCheck (runtime, {}), "real asynchronous source verification finishes");
    snapshotCopyEntered = snapshotCopyReleased = nullptr;
    require (quietNotice, "normal long copy preparation never reports the next source unavailable");
    const auto sourceFile = root.getChildFile ("alternate.wav"), saved = root.getChildFile ("held-source.wav");
    require (sourceFile.moveFileTo (saved), "make only the published path briefly unreadable after verification");
    control::service (comparison); // A different refresh consumes the finished verification.
    const auto prepared = control::preparedSourceFor (runtime, {});
    require (saved.moveFileTo (sourceFile), "restore the fixture path before checking the handoff");
    require (prepared && revision::snapshotCopiesForTest.load() == copies + 1,
             "playback preparation inherits the verified copy without rereading the source");
    require (comparison.snapshot().bSelected && comparison.pendingSlot() == 2,
             "temporary source loss at the asynchronous handoff keeps old C and the pending choice");
    control::probeSourceNow (*prepared);
    comparison.servicePendingAudition (-25, -6, true);
    bool rendered = false;
    for (int n = 0; n < 4; ++n)
    {
        // The approved transition includes a bounded return to A.
        block.clear(); comparison.observeTransport (cursor, true, true);
        rendered = comparison.renderSelectedB (block, cursor, true, true, true);
        cursor += 4096; control::service (comparison); comparison.servicePendingAudition (-25, -6, true);
    }
    require (rendered && std::abs (block.getSample (0, 1023) - 0.3539729f) < 1.0e-6f
             && comparison.snapshot().checkSelection->playbackIdentity != state.checkSelection->playbackIdentity,
             "the prepared next C reaches the actual controller publication");
    comparison.selectA();
}

inline void testReferenceAsyncCapture (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    namespace revision = ref::revision_detail;
    const auto root = sandbox.getChildFile ("async-source-capture");
    require (root.createDirectory().wasOk(), "isolated asynchronous A capture fixture");
    const auto file = root.getChildFile ("version.wav");
    const juce::String recording = "22222222-2222-4222-8222-222222222222";
    const juce::String version = "33333333-3333-4333-8333-333333333333";
    const auto fixture = makeWholeSongFixture (root, file, recording, version);
    auto preset = bindRuntimeV2WorkVersionPresetToSource (
        "88888888-8888-4888-8888-888888888888", "99999999-9999-4999-8999-999999999999",
        fixture.receipt, juce::SHA256 (file).toHexString(), fixture.pcmHash, recording, version, fixture.audio.getNumSamples());
    preset.getDynamicObject()->setProperty ("format", "kirin_hypha_reference_library_preset");
    preset.getDynamicObject()->setProperty ("version", "1.0");
    preset.getDynamicObject()->removeProperty ("work_id");
    preset.getDynamicObject()->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    require (writeJson (root.getChildFile ("library/manifest.json"), independentLibraryManifest (root, preset, 1)), "publish an actual Version entry");
    ref::RuntimeV2Controller runtime (root, {}, true);
    require (control::stopFor (runtime, {}), "drive the actual A capture worker deterministically");
    runtime.configure ({"async-capture-post", {}, 42, true}, 48000, 2);
    control::serviceFor (runtime, {});
    require (!runtime.snapshot().versions.empty() && runtime.selectLibraryVersion (runtime.snapshot().versions.front().id), "select the actual Version entry");
    runtime.setContentObservationEnabled (true);
    const auto& capture = control::captureFor (runtime, {});
    const auto continuity = capture.continuity();
    juce::WaitableEvent entered, release;
    snapshotCopyEntered = &entered; snapshotCopyReleased = &release;
    const auto copies = revision::snapshotCopiesForTest.load();
    revision::snapshotCopyHookForTest.store (&holdSnapshotCopy);
    control::serviceFor (runtime, {});
    const bool copying = entered.wait (30000);
    juce::AudioBuffer<float> input (2, 4096);
    if (copying) for (int n = 0; n < 64; ++n)
    {
        for (int ch = 0; ch < 2; ++ch) input.copyFrom (ch, 0, fixture.audio, ch, n * 4096, 4096);
        runtime.observeTransport (n * 4096, true, true);
        runtime.observeAInput (input, n * 4096, true, true, true);
        control::serviceFor (runtime, {});
    }
    release.signal();
    require (copying && control::waitForSourceCheck (runtime, {}), "real asynchronous Version copy completes");
    snapshotCopyEntered = snapshotCopyReleased = nullptr;
    require (capture.currentAudio() && capture.observationQueueDrained() && capture.continuity() == continuity,
             "the same A worker consumes a full window without resets while its source copy is blocked");
    control::serviceFor (runtime, {});
    require (control::currentPagesFor (runtime, {}).sourceOpen() && revision::snapshotCopiesForTest.load() == copies + 1,
             "the next A worker refresh prepares playback from the single verified copy");
}

inline void testReferenceSourceConcurrency (const juce::File& root, const ref::RuntimeSource& original)
{
    namespace revision = ref::revision_detail;
    const juce::File input (original.absolutePath);
    testReferenceSourceShutdown (input);
    auto slow = original;
    const auto slowFile = root.getChildFile ("slow-copy.wav");
    require (input.copyFileTo (slowFile), "separate slow snapshot fixture");
    slow.absolutePath = slowFile.getFullPathName();
    slow.preparedAudio = std::make_shared<revision::PreparedSourceAudio>();
    require (readMonitorRevision (slowFile, slow.revision), "slow copy revision");
    auto duplicate = slow;
    duplicate.preparedAudio = std::make_shared<revision::PreparedSourceAudio>();
    auto ready = original;
    ready.preparedAudio = std::make_shared<revision::PreparedSourceAudio>();
    const auto prepared = revision::snapshotFor (ready);
    require (prepared->available(), "prepare another source before blocking I/O");
    // Use another receipt owner to exercise the shared cache as well as the per-source cache.
    auto readyAgain = ready;
    readyAgain.preparedAudio = std::make_shared<revision::PreparedSourceAudio>();
    juce::WaitableEvent entered, release;
    snapshotCopyEntered = &entered; snapshotCopyReleased = &release;
    revision::snapshotCopyHookForTest.store (&holdSnapshotCopy);
    auto copying = std::async (std::launch::async, [&] { return revision::snapshotFor (slow); });
    const bool started = entered.wait (30000);
    auto existing = std::async (std::launch::async, [&] { return revision::snapshotFor (readyAgain); });
    auto sameSource = std::async (std::launch::async, [&] { return revision::snapshotFor (duplicate); });
    const bool independent = existing.wait_for (std::chrono::seconds (2)) == std::future_status::ready;
    release.signal();
    const auto copied = copying.get(), reused = existing.get(), shared = sameSource.get();
    snapshotCopyEntered = snapshotCopyReleased = nullptr;
    revision::snapshotCopyHookForTest.store (nullptr);
    require (started && independent && reused == prepared,
             "a stalled copy cannot block a different source's prepared snapshot");
    require (copied->available() && copied == shared, "concurrent owners of one source share its verified copy");

    require (revision::service().waitForIdle (std::chrono::seconds (30)), "shared monitor finishes before clock simulation");
    juce::MemoryBlock bytes;
    require (slowFile.loadFileAsData (bytes), "keep slow monitor fixture bytes");
    struct Reset
    {
        ~Reset()
        {
            revision::hashReadHook.store (nullptr);
            revision::unchangedMetadataForTest.store (nullptr);
            revision::monitorClockOffsetMsForTest.store (0);
        }
    } reset;
    for (const bool mismatch : { false, true })
    {
        if (mismatch)
        {
            static_cast<char*> (bytes.getData())[bytes.getSize() - 1] ^= 1;
            juce::FileOutputStream output (slowFile);
            require (output.openedOk() && output.setPosition (0) && output.write (bytes.getData(), bytes.getSize()),
                     "rewrite only the monitor fixture");
            output.flush(); require (output.getStatus().wasOk(), "monitor fixture write completes");
        }
        revision::unchangedMetadataForTest.store (&slow);
        revision::Service monitor;
        const auto authority = monitor.watch (slow);
        monitor.advanceMonitorForTest (slow, mismatch ? 4000 : 0);
        monitorHashReads.store (0);
        revision::hashReadHook.store (&finishSlowSourceHash);
        monitor.monitor (slow, &readMonitorRevision);
        require (monitor.waitForCompletion (slow, std::chrono::seconds (30)), "simulated long hash completes");
        require (monitorHashReads.load() == 1 && (authority->unavailable.load() != 0) == mismatch,
                 "long hash checks the real bytes on success and mismatch");
        const auto poll = [&] {
            monitor.pollMonitorForTest (slow);
            revision::hashReadHook.store (&countMonitorHash);
            monitor.monitor (slow, &readMonitorRevision);
            require (monitor.waitForCompletion (slow, std::chrono::seconds (30)), "bounded monitor poll completes");
        };
        poll();
        require (monitorHashReads.load() == 1, "a long hash cannot expire its own retry deadline");
        revision::monitorClockOffsetMsForTest.fetch_add (60000);
        poll();
        require (monitorHashReads.load() == 1, "unchanged normal and refused sources do not rehash within a minute");
        revision::monitorClockOffsetMsForTest.fetch_add (240100);
        poll();
        require (monitorHashReads.load() == 2, "the next audit runs once after the interval from completion");
        poll();
        require (monitorHashReads.load() == 2, "a confirmed mismatch also schedules its next audit from completion");
    }
}

// Four real sources fill the normal receipt cache. The fourth can read directly
// when temporary capacity is unavailable; the selected third source keeps sounding.
inline void testReferenceCachedSnapshotRecovery (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    namespace revision = ref::revision_detail;
    const auto root = sandbox.getChildFile ("cached-snapshot-recovery");
    require (root.createDirectory().wasOk(), "isolated cache recovery fixture");
    auto preset = publishTwoCheckLibrary (root, 2, true);
    for (const auto* id : {"cccccccc-cccc-4ccc-8ccc-cccccccccccc", "dddddddd-dddd-4ddd-8ddd-dddddddddddd"})
    {
        const auto file = root.getChildFile (juce::String (id) + ".wav");
        require (root.getChildFile ("source.wav").copyFileTo (file), "distinct immutable source path");
        const auto hash = juce::SHA256 (file).toHexString();
        auto descriptor = makeRuntimeV2Source (file, hash, juce::String::repeatedString ("1", 64));
        addRuntimeV2MeasurementSummary (descriptor, -22, -6);
        const auto other = bindRuntimeV2PresetToSource ("88888888-8888-4888-8888-888888888888",
            "99999999-9999-4999-8999-999999999999", stageWholeSongArtifact (root, "sources", descriptor),
            hash, juce::String::repeatedString ("1", 64));
        auto check = preset["checks"][0].clone();
        check.getDynamicObject()->setProperty ("check_id", id);
        for (const auto* key : {"source_identity", "source_artifact"})
            check["candidates"][0].getDynamicObject()->setProperty (key, other["checks"][0]["candidates"][0][key]);
        preset["checks"].getArray()->add (check);
    }
    require (writeJson (root.getChildFile ("library/manifest.json"), libraryManifest (root, preset, 2)), "publish four sources");
    ref::ReferenceComparisonController c (root);
    control::stopWorkers (c); c.configure ({"cache-recovery-post", {}, 42, true}, 48000, 2);
    require (((juce::Thread&) control::sourceVisual (c, {})).stopThread (-1), "deterministic visual owner");
    c.setPresented (true); control::service (c); c.observeTransport (0, true, true); control::service (c);
    auto& runtime = control::roleFor (c, {2});
    const auto choices = c.snapshot().checkTargets;
    require (choices.size() == 4, "all real Check choices arrive");
    for (int index = 1; index < 3; ++index)
    {
        require (c.selectCheck (choices[static_cast<size_t> (index)].id), "prepare another cached source");
        for (int poll = 0; poll < 4; ++poll) control::service (c);
        require (c.snapshot().checkReady && runtime.snapshot().checkId == preset["checks"][index]["check_id"].toString(),
                 "each source reaches the actual playback publication");
    }
    struct Reset { ~Reset() { revision::snapshotFreeBytesForTest.store (-1); } } reset;
    const auto baseline = revision::SnapshotStorage::reservedForTest();
    const auto length = root.getChildFile ("source.wav").getSize();
    revision::snapshotFreeBytesForTest.store (revision::SnapshotStorage::headroom + baseline + length / 2);
    const auto audible = runtime.visualBinding().source;
    const auto copy = audible->preparedAudio->snapshot;
    const auto audibleFile = copy->fileForTest();
    require (c.requestAudition (2, -22, -6), "play the third verified source");
    juce::AudioBuffer<float> block (2, 512);
    const auto feed = [&] {
        for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (block.getWritePointer (ch), 0.125f, 512);
        c.observeTransport (0, true, true); c.observeAInput (block, 0, true, true, true);
        beginReferenceRtProbe();
        const auto rendered = c.renderSelectedB (block, 0, true, true, true);
        require (endReferenceRtProbe() == 0, "cache recovery allocates nothing on the audio callback");
        control::service (c); return rendered;
    };
    require (feed() && feed() && block.getSample (0, 511) == 0.25f, "the third source really sounds before reclamation");
    require (c.selectCheck (choices[3].id), "request the fourth source through the actual controller");
    const auto generation = runtime.requestedGeneration();
    control::service (c);
    require (revision::SnapshotStorage::reservedForTest() <= baseline && audibleFile.existsAsFile(),
             "authenticated fallback preserves all active copies without allocating disk space");
    for (int poll = 0; poll < 4; ++poll)
        require (feed() && block.getSample (0, 511) == 0.25f && audibleFile.existsAsFile(),
                 "reclamation and retry preserve the selected PCM without requiring more disk space");
    require (runtime.awaitingPreparedLevel() && control::preparedSourceFor (runtime, {}),
             "the fourth source prepares without requiring additional temporary capacity");
    for (int poll = 0; poll < 6; ++poll)
    { c.servicePendingAudition (-22, -6, true); feed(); }
    require (runtime.snapshot().checkId == preset["checks"][3]["check_id"].toString()
        && runtime.requestedGeneration() == generation && c.snapshot().bSelected,
        "the same fourth-source choice starts without another click or free-space increase");
    c.selectA();
}
