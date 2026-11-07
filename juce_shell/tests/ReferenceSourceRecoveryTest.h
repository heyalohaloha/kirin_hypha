#pragma once
#include "ReferenceManualService.h"
#include "ReferenceCheckSwitchTest.h"
#include "ReferenceSourceCtimeTest.h"
#include "ReferenceSourceConcurrencyTest.h"

inline std::atomic<int> cachedFailureHashReads { 0 };
inline void countCachedFailureHash() { cachedFailureHashReads.fetch_add (1); }
inline juce::File growingSnapshotFile;
inline void growSnapshotSource()
{
    juce::FileOutputStream output (growingSnapshotFile);
    require (output.openedOk() && output.writeByte (0x7f), "grow the fixture after its source handle was opened");
    output.flush(); require (output.getStatus().wasOk(), "fixture append completes");
}

inline void testReferenceSourceRecovery (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile ("source-recovery");
    require (root.createDirectory().wasOk(), "isolated source recovery fixture");
    publishTwoCheckLibrary (root, 2, true);
    ref::ReferenceComparisonController comparison (root);
    control::stopWorkers (comparison);
    comparison.configure ({"source-recovery-post", {}, 42, true}, 48000, 2);
    comparison.setPresented (true); control::service (comparison);
    comparison.observeTransport (0, true, true); control::service (comparison);
    auto& runtime = control::roleFor (comparison, {2});
    const auto source = runtime.visualBinding().source;
    require (source && comparison.snapshot().checkReady, "actual Check entry prepares the source");
    for (int role = 1; role <= 3; ++role)
        control::stopPageRefill (control::currentPagesFor (control::roleFor (comparison, {role}), {}));
    require (((juce::Thread&) control::sourceVisual (comparison, {})).stopThread (-1), "deterministic visual owner");
    {
        auto identity = std::make_shared<std::atomic<std::uint64_t>> (7);
        ref::AudioPages tokenPages (ref::AudioPages::ServiceMode::manual);
        require (tokenPages.open (*source, 48000, 2, true, {}, ref::revision_detail::snapshotInput (*source), {}, {},
            identity, identity.get(), 7).isEmpty(), "open actual pages with a nonzero initial authority token");
        tokenPages.service(); require (!tokenPages.sourceUnavailable(), "a matching nonzero token is available");
        for (const auto token : { std::uint64_t {8}, std::uint64_t {0} })
        {
            identity->store (token); tokenPages.service();
            require (tokenPages.sourceUnavailable(), "each changed token pauses pages, including zero");
            identity->store (7); tokenPages.service();
            require (!tokenPages.sourceUnavailable(), "the initial authority token can recover automatically");
        }
    }
    auto& revisions = ref::revision_detail::service();
    const auto probe = [&] (std::int64_t missingMs = 0, bool rehash = true) {
        require (revisions.waitForIdle (std::chrono::seconds (30)), "preceding source work completes");
        revisions.advanceMonitorForTest (*source, missingMs, rehash);
        ref::RuntimeV2SourceRepository repository (root);
        repository.verifySourceRevision (*source);
        require (revisions.waitForCompletion (*source, std::chrono::seconds (30)), "source probe completes");
        control::service (comparison);
    };
    probe();
    juce::AudioBuffer<float> block (2, 256);
    const auto render = [&] {
        block.clear(); block.addSample (0, 0, 0.375f);
        comparison.observeTransport (0, true, true);
        beginReferenceRtProbe();
        const bool sounded = comparison.renderSelectedB (block, 0, true, true, true);
        require (endReferenceRtProbe() == 0, "source retry performs no allocation on Audio Thread");
        if (!sounded) require (block.getSample (0, 0) == 0.375f, "unavailable source preserves A");
        return sounded;
    };
    require (comparison.requestAudition (2, -22, -6) && render(), "actual C plays before failure");
    const auto ticket = runtime.normalSelectionTicket();
    struct Reset { ~Reset() {
        ref::revision_detail::persistentPathFailureForTest.store (false);
        ref::revision_detail::unchangedMetadataForTest.store (nullptr);
        ref::revision_detail::snapshotCopyHookForTest.store (nullptr);
        ref::revision_detail::hashReadHook.store (nullptr);
    } } reset;
    ref::revision_detail::persistentPathFailureForTest.store (true);
    probe();
    require (render() && comparison.snapshot().bSelected, "one unavailable stat preserves audible C");
    probe (4000);
    require (comparison.snapshot().checkSelection->auditionPages == ref::AuditionPageState::sourceUnavailable,
             "confirmed loss publishes only an atomic source status to the view");
    require (!render() && runtime.normalSelectionTicket() == ticket && comparison.snapshot().bSelected,
             "sustained loss pauses only output and keeps the explicit selection");
    ref::revision_detail::persistentPathFailureForTest.store (false);
    probe();
    require (render() && runtime.normalSelectionTicket() == ticket
             && comparison.snapshot().checkSelection->auditionPages != ref::AuditionPageState::sourceUnavailable,
             "the same C resumes automatically and clears its source notice after stat returns");
    // Re-selection can verify the file before the ordinary monitor sees recovery.
    // Cover fresh and cached initial verification, and the changed-metadata worker.
    for (int recovery = 0; recovery < 3; ++recovery)
    {
        ref::revision_detail::persistentPathFailureForTest.store (true); probe (4000);
        require (!render(), "a sustained outage precedes the explicit re-verification");
        ref::revision_detail::persistentPathFailureForTest.store (false);
        ref::RuntimeV2SourceRepository repository (root);
        if (recovery == 2)
        {
            const juce::File file (source->absolutePath);
            require (file.setLastModificationTime (file.getLastModificationTime() + juce::RelativeTime::seconds (2)),
                     "change only metadata to exercise the asynchronous verifier");
            require (repository.verifySourceFile (*source, true) == "reference_source_revalidating",
                     "explicit verification starts the changed-metadata worker");
            require (revisions.waitForCompletion (*source, std::chrono::seconds (30)), "explicit worker verification finishes");
        }
        require (repository.verifySourceFile (*source, recovery == 0).isEmpty(), "the explicit source verification succeeds");
        ref::revision_detail::persistentPathFailureForTest.store (true);
        probe (0, false);
        require (render() && runtime.normalSelectionTicket() == ticket && comparison.snapshot().bSelected,
                 "one stat failure after explicit recovery starts a fresh grace period and keeps actual C output");
        probe (4000);
        require (!render(), "a new sustained loss still pauses unverified output");
        ref::revision_detail::persistentPathFailureForTest.store (false); probe();
        require (render(), "monitor recovery resumes the same selection after the new outage");
    }
    const auto pathChecks = ref::revision_detail::pathChecksForTest.load();
    for (int n = 0; n < 40; ++n) control::service (comparison);
    require (revisions.waitForIdle (std::chrono::seconds (30)), "bounded probes finish");
    require (ref::revision_detail::pathChecksForTest.load() - pathChecks < 10, "page fills and control polls share a bounded path probe");
    juce::File file (source->absolutePath);
    juce::MemoryBlock original;
    require (file.loadFileAsData (original), "keep fixture bytes for automatic recovery");
    const auto growingFile = root.getChildFile ("growing-source.wav");
    require (file.copyFileTo (growingFile), "separate snapshot-growth fixture");
    auto growingSource = *source; growingSource.absolutePath = growingFile.getFullPathName();
    require (ref::RuntimeV2SourceRepository (root).readSourceRevision (growingSource, growingSource.revision), "growth fixture revision");
    growingSnapshotFile = growingFile;
    ref::revision_detail::snapshotCopyHookForTest.store (&growSnapshotSource);
    ref::revision_detail::SourceSnapshot growing (growingSource);
    require (!growing.available() && growing.copiedBytesForTest == growingSource.revision.sizeBytes.getLargeIntValue(),
             "a concurrent append cannot extend the copy beyond the registered source length");
    growingSnapshotFile = juce::File {};
#if JUCE_WINDOWS
    // Exercise both directions of Windows sharing while the handles overlap.
    ref::revision_detail::OpenFile verifier (file);
    ref::RuntimeFileRevision sharingRevision;
    require (verifier.revision (sharingRevision), "hold the actual verification handle open");
    const auto writer = CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_WRITE | DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    require (writer != INVALID_HANDLE_VALUE, "verification permits a concurrent Windows writer and delete access");
    require (ref::RuntimeV2SourceRepository (root).readSourceRevision (*source, sharingRevision),
             "the real path revision read permits an already open write/delete handle");
    DWORD written = 0;
    require (WriteFile (writer, original.getData(), 1, &written, nullptr) && written == 1 && FlushFileBuffers (writer),
             "overwrite with exact fixture bytes while the verification handle remains open");
    probe();
    require (render() && verifier.revision (sharingRevision), "monitor verification works with both handles still open");
    require (CloseHandle (writer) != 0, "close only the fixture writer");
#endif
    auto altered = original;
    static_cast<char*> (altered.getData())[altered.getSize() - 1] ^= 1;
    const auto overwrite = [&] (const juce::MemoryBlock& bytes) {
        juce::FileOutputStream output (file);
        return output.openedOk() && output.setPosition (0) && output.write (bytes.getData(), bytes.getSize())
            && (output.flush(), output.getStatus().wasOk());
    };
    sourcePageMutationTarget = &file;
    ref::revision_detail::hashReadHook.store (&changeAttributesDuringPageRead);
    const auto invalidations = revisions.watch (*source)->invalidation.load();
    probe();
    require (ref::revision_detail::hashReadHook.load() == nullptr && render()
             && revisions.watch (*source)->invalidation.load() == invalidations,
             "attributes changed during a real monitor hash do not interrupt the verified C");
    sourcePageMutationTarget = nullptr;
    probe(); require (render(), "a stable metadata retry keeps the same C audible");
    {
        juce::FileOutputStream output (file);
        require (output.openedOk() && output.setPosition (0) && output.truncate().wasOk(), "truncate during a fixture rewrite");
    }
    auto anotherReader = *source;
    anotherReader.preparedAudio = std::make_shared<ref::revision_detail::PreparedSourceAudio>();
    require (ref::RuntimeV2SourceRepository (root).verifySourceFile (anotherReader, true) == "reference_source_changed",
             "a second reader refuses the incomplete file at its real initial verification entry");
    require (render() && runtime.normalSelectionTicket() == ticket
             && revisions.watch (*source)->unavailable.load() == 0,
             "refusing a new reader cannot bypass the audible source's retry window");
    probe(); require (render(), "a monitor poll during truncate keeps the immutable audible copy");
    require (overwrite (original), "finish the brief rewrite with the exact registered bytes");
    probe(); require (render() && runtime.normalSelectionTicket() == ticket,
                      "the exact bytes return before the retry window without interrupting C");
    ref::revision_detail::persistentPathFailureForTest.store (true); probe (4000);
    ref::revision_detail::persistentPathFailureForTest.store (false);
    ref::revision_detail::unchangedMetadataForTest.store (source.get());
    require (overwrite (altered), "overwrite the same path while C is selected");
    ref::RuntimeFileRevision unchanged;
    require (ref::RuntimeV2SourceRepository (root).readSourceRevision (*source, unchanged)
             && ref::revision_detail::equal (unchanged, source->revision), "all five observed fields still match");
    // The actual controller's periodic hash must find this rewrite too.
    auto staleMetadata = *source;
    ref::revision_detail::SourceSnapshot untrusted (staleMetadata);
    require (!untrusted.available(), "same stored metadata cannot authorize changed audio bytes");
    probe();
    require (!render() && comparison.snapshot().bSelected
        && comparison.snapshot().checkSelection->sourceFailureReason == "reference_source_changed",
        "confirmed changed content keeps both the choice and its exact reason through the actual runtime snapshot");
    cachedFailureHashReads.store (0);
    ref::revision_detail::hashReadHook.store (&countCachedFailureHash);
    probe (0, false);
    require (cachedFailureHashReads.load() == 0, "a bad file returned after path loss uses the cached hash failure instead of rereading the NAS");
    ref::revision_detail::hashReadHook.store (nullptr);
    ref::revision_detail::unchangedMetadataForTest.store (nullptr);
    require (overwrite (original), "restore exact bytes at the same path");
    probe();
    require (render() && runtime.normalSelectionTicket() == ticket, "restored exact bytes clear the refusal without another click");
    const auto saved = file.getSiblingFile ("saved-source.wav");
    require (file.moveFileTo (saved), "actually remove the source path");
    probe(); require (render(), "a brief actual disappearance keeps the immutable audible copy");
    probe (4000); require (!render(), "confirmed disappearance pauses output");
    require (saved.moveFileTo (file), "restore the same source object");
    probe(); require (render(), "the actual path return resumes the held selection");
    testReferenceSourceConcurrency (root, *source);
    testReferenceSnapshotStorage (root, *source);
    ref::revision_detail::persistentPathFailureForTest.store (true); probe (4000);
    require (!render(), "the local source awaits recovery before another monitor stalls");
    ref::revision_detail::persistentPathFailureForTest.store (false);
    auto stalled = *source;
    const auto stalledFile = root.getChildFile ("stalled-monitor.wav");
    require (file.copyFileTo (stalledFile), "independent stalled monitor fixture");
    stalled.absolutePath = stalledFile.getFullPathName();
    require (readMonitorRevision (stalledFile, stalled.revision), "stalled monitor revision");
    sourceHashEntered = false; sourceHashRelease = false;
    ref::revision_detail::hashReadHook.store (&pauseSourceHash);
    revisions.advanceMonitorForTest (stalled);
    ref::RuntimeV2SourceRepository (root).verifySourceRevision (stalled);
    bool entered = false;
    {
        std::unique_lock<std::mutex> lock (sourceHashPauseMutex);
        entered = sourceHashPauseCondition.wait_for (lock, std::chrono::seconds (30), [] { return sourceHashEntered; });
    }
    revisions.advanceMonitorForTest (*source);
    ref::RuntimeV2SourceRepository (root).verifySourceRevision (*source);
    const bool recovered = revisions.waitForCompletion (*source, std::chrono::seconds (2));
    bool sounded = false;
    if (recovered) { control::service (comparison); sounded = render(); }
    { std::lock_guard<std::mutex> lock (sourceHashPauseMutex); sourceHashRelease = true; }
    sourceHashPauseCondition.notify_all();
    require (revisions.waitForCompletion (stalled, std::chrono::seconds (30)), "release only the fixture's stalled hash");
    require (entered && recovered && sounded && runtime.normalSelectionTicket() == ticket,
             "a blocked real monitor hash cannot delay another source's recovery and actual C output");
    // Recover the monitor while the page worker still publishes the old unavailable state.
    // This is the actual queued C entry, without waiting for a refreshed UI snapshot.
    for (const bool resume : { false, true })
    {
        ref::revision_detail::persistentPathFailureForTest.store (true); probe (4000);
        if (resume)
        {
            comparison.observeTransport (0, true, false);
            comparison.renderSelectedB (block, 0, true, true, true);
            control::service (comparison);
            require (runtime.hasHeldSelection(), "transport stop preserves the selected C for resume");
        }
        else
        {
            comparison.selectA();
            require (comparison.requestAudition (2, -22, -6), "reserve C during source recovery");
        }
        comparison.observeTransport (0, true, true);
        comparison.observeAInput (block, 0, true, true, true);
        render();
        comparison.servicePendingAudition (-22, -6, true);
        require (comparison.snapshot().pendingAudition.waiting(), "queued and resumed C wait while the source is unavailable");
        ref::revision_detail::persistentPathFailureForTest.store (false);
        revisions.advanceMonitorForTest (*source);
        ref::RuntimeV2SourceRepository (root).verifySourceRevision (*source);
        require (revisions.waitForCompletion (*source, std::chrono::seconds (30)), "monitor recovers ahead of the page worker");
        // A stop revokes the runtime publication. Republish through its real refresh
        // before allowing the page worker to update its older unavailable observation.
        if (resume) control::refreshOnlyFor (runtime, {});
        if (!resume)
        {
            require (runtime.snapshot().auditionPages == ref::AuditionPageState::sourceUnavailable,
                     "page worker still publishes the unavailable state at the recovery boundary");
            comparison.servicePendingAudition (-22, -6, true);
            require (comparison.snapshot().pendingAudition.waiting(), "monitor recovery cannot cancel the queued C intent");
        }
        control::service (comparison);
        comparison.servicePendingAudition (-22, -6, true);
        require (render() && !comparison.snapshot().pendingAudition.waiting(), "C starts after page recovery without another click");
    }
    comparison.selectA();
    testReferenceAsyncSourcePreparation (sandbox);
    testReferenceAsyncCapture (sandbox);
}
