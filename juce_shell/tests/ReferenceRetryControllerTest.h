#pragma once
#include "ReferenceCheckSwitchTest.h"
#include "../src/reference_audition/ReferencePinnedSource.h"
#include "../src/reference_audition/ReferenceVerifiedReader.h"

// An uncached source whose timestamp differs from its receipt, with every hash read failing:
// the worker's failure must reach the workspace retry clock, polls inside a reservation start
// no hash, an expired reservation starts exactly one, polls during that attempt only wait, and
// the unchanged file becomes ready again once reads recover. Driven through the real controller.
inline void testReferenceRetryController (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile ("source-retry-controller");
    require (root.createDirectory().wasOk(), "isolated retry controller fixture");
    const auto base = makeWholeSongFixture (root, root.getChildFile ("input.wav"),
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333");
    const auto manifest = hypha::tests::version_formats::publish (root, base.audio, 48000, false, 0, 1);
    require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "publish retry fixture");
    const auto file = root.getChildFile ("version-1.wav");
    require (file.setLastModificationTime (file.getLastModificationTime() - juce::RelativeTime::seconds (20)),
             "change only the source timestamp before the first load");
    const auto id = manifest["versions"][0]["entry_id"].toString();
    ref::ReferenceComparisonController c (root);
    control::stopWorkers (c); c.configure ({"retry-post", {}, 42, true}, 48000, 2);
    require (((juce::Thread&) control::sourceVisual (c, {})).stopThread (-1), "stop the visual worker for a deterministic hash count");
    control::service (c);
    require (c.selectVersion (id + "/" + id + "/" + id), "select the uncached source");
    auto& runtime = control::roleFor (c, { 1 });
    auto& service = ref::revision_detail::service();
    require (service.waitForIdle (std::chrono::seconds (30)), "no earlier hash is still running");
    struct Reset { ~Reset() { ref::revision_detail::persistentReadFailureForTest.store (false); } } reset;
    ref::revision_detail::persistentReadFailureForTest.store (true);
    auto started = service.startedCount();
    control::service (c);
    require (service.waitForIdle (std::chrono::seconds (30)), "first worker attempt finishes");
    require (service.startedCount() == started + 1, "the first verification starts one hash");
    for (int retry = 0; retry < 2; ++retry)
    {
        const auto before = juce::Time::currentTimeMillis();
        control::service (c);
        require (control::sourceRetryDeadline (runtime, {}) >= before + (retry == 0 ? 5000 : 10000),
                 "worker failure reserves the retry clock");
        started = service.startedCount();
        for (int poll = 0; poll < 12; ++poll) control::service (c);
        require (service.startedCount() == started, "polls during the reservation start no hash");
        require (c.snapshot().versionSelection->state == ref::RuntimeState::waiting
                 && c.snapshot().versionSelection->rejectionCode == "reference_source_read_failed",
                 "the unverified source waits silently");
        sourceHashEntered = false; sourceHashRelease = false;
        ref::revision_detail::hashReadHook.store (&pauseSourceHash);
        control::sourceRetryDeadline (runtime, {}) = juce::Time::currentTimeMillis() - 1; // Advance only the fixture's retry clock.
        control::service (c);
        {
            std::unique_lock<std::mutex> lock (sourceHashPauseMutex);
            require (sourceHashPauseCondition.wait_for (lock, std::chrono::seconds (30), [] { return sourceHashEntered; }),
                     "an expired reservation starts a hash on the worker");
        }
        require (service.startedCount() == started + 1, "an expired reservation starts exactly one hash");
        for (int poll = 0; poll < 4; ++poll) control::service (c);
        require (service.startedCount() == started + 1 && control::sourceRetryDeadline (runtime, {}) == 0,
                 "polls during the attempt wait without another hash or reservation");
        { std::lock_guard<std::mutex> lock (sourceHashPauseMutex); sourceHashRelease = true; }
        sourceHashPauseCondition.notify_all();
        require (service.waitForIdle (std::chrono::seconds (30)), "retry attempt finishes");
    }
    const auto before = juce::Time::currentTimeMillis();
    control::service (c);
    require (control::sourceRetryDeadline (runtime, {}) >= before + 20000, "repeated failure reserves a longer retry");
    ref::revision_detail::persistentReadFailureForTest.store (false);
    control::sourceRetryDeadline (runtime, {}) = juce::Time::currentTimeMillis() - 1;
    control::service (c);
    require (service.waitForIdle (std::chrono::seconds (30)), "recovery attempt finishes");
    juce::AudioBuffer<float> input (2, 256);
    for (int cursor = 0; cursor < 192000; cursor += 256)
    {
        for (int ch = 0; ch < 2; ++ch) input.copyFrom (ch, 0, base.audio, ch, cursor, 256);
        c.observeTransport (cursor, true, true); c.observeAInput (input, cursor, true, true, true);
        control::service (c);
    }
    require (c.snapshot().versionReady && c.snapshot().versionSelection->rejectionCode.isEmpty(),
             "the unchanged source becomes ready after reads recover");
}

// A prepared Check shares the same retry monitor: the current C remains audible until the
// requested source can be confirmed, then the same request completes without another click.
inline void testReferencePreparedSourceRetry (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile ("source-retry-prepared");
    require (root.createDirectory().wasOk(), "isolated prepared retry fixture");
    const auto preset = publishTwoCheckLibrary (root, 2, true);
    const auto secondCheckId = preset["checks"][1]["check_id"].toString();
    ref::ReferenceComparisonController c (root);
    control::stopWorkers (c); c.configure ({"prepared-retry-post", {}, 42, true}, 48000, 2);
    require (((juce::Thread&) control::sourceVisual (c, {})).stopThread (-1), "stop the visual worker for a deterministic hash count");
    c.setPresented (true); control::service (c);
    c.observeTransport (0, true, true); control::service (c);
    const auto state = c.snapshot();
    require (state.checkReady && state.checkTargets.size() >= 2, "two playable Checks are available");
    juce::AudioBuffer<float> block (2, 512);
    int cursor = 0;
    bool levelAvailable = true;
    const auto feed = [&]
    {
        for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (block.getWritePointer (ch), 0.125f, 512);
        c.observeTransport (cursor, true, true); c.observeAInput (block, cursor, true, true, true);
        const bool rendered = c.renderSelectedB (block, cursor, true, true, true);
        cursor += 512; control::service (c);
        c.servicePendingAudition (levelAvailable ? -25.0 : std::numeric_limits<double>::quiet_NaN(), -6, true);
        return rendered;
    };
    feed(); require (c.requestAudition (2, -22, -6), "start C explicitly");
    require (feed(), "positive control: C renders before switching");
    require (c.selectCheck (state.checkTargets[1].id), "change the Check while C is playing");
    auto& runtime = control::roleFor (c, { 2 });
    const auto generation = runtime.requestedGeneration();
    levelAvailable = false; // Withhold the return so the next Check stays in preparation.
    for (int n = 0; n < 8; ++n) require (feed(), "C keeps playing while the next Check prepares");
    require (runtime.awaitingPreparedLevel(), "the next Check is prepared and waits for its return");
    auto& service = ref::revision_detail::service();
    require (service.waitForIdle (std::chrono::seconds (30)), "no earlier hash is still running");
    struct Reset { ~Reset() { ref::revision_detail::persistentReadFailureForTest.store (false); } } reset;
    const auto file = root.getChildFile ("alternate.wav");
    ref::revision_detail::persistentReadFailureForTest.store (true);
    require (file.setLastModificationTime (file.getLastModificationTime() + juce::RelativeTime::seconds (2)),
             "change only the next source's timestamp during preparation");
    const auto prepared = control::preparedSourceFor (runtime, {});
    require (prepared != nullptr, "next Check retains its prepared source");
    const auto probe = [&] (std::int64_t missingMs = 0) {
        require (service.waitForIdle (std::chrono::seconds (30)), "previous source probe completes");
        service.advanceMonitorForTest (*prepared, missingMs);
        ref::RuntimeV2SourceRepository (root).verifySourceRevision (*prepared);
        require (service.waitForCompletion (*prepared, std::chrono::seconds (30)), "prepared source probe completes");
    };
    probe();
    for (int n = 0; n < 4; ++n) require (feed(), "a transient next-source read failure keeps C audible");
    probe (4000); levelAvailable = true;
    for (int n = 0; n < 4; ++n) require (feed(), "an unavailable next source does not cut the verified current C");
    require (runtime.requestedGeneration() == generation, "the same next Check remains requested");
    require (c.snapshot().checkSelection->pendingSourceFailure.isNotEmpty(),
             "a persistent next-source failure is published separately from the audible C");
    require (c.requestAudition (2, -25, -6) && feed()
             && c.snapshot().checkSelection->pendingSourceFailure.isNotEmpty(),
             "pressing C again keeps the failure notice and the same requested switch");
    ref::revision_detail::persistentReadFailureForTest.store (false);
    juce::MemoryBlock original;
    require (file.loadFileAsData (original), "retain the next-source fixture bytes");
    auto altered = original; static_cast<char*> (altered.getData())[altered.getSize() - 1] ^= 1;
    const auto overwrite = [&] (const juce::MemoryBlock& bytes) {
        juce::FileOutputStream out (file);
        require (out.openedOk() && out.setPosition (0) && out.write (bytes.getData(), bytes.getSize()), "overwrite only the prepared next-source fixture");
        out.flush(); require (out.getStatus().wasOk(), "flush the next-source fixture");
    };
    overwrite (altered); probe (4000);
    require (feed() && c.snapshot().checkSelection->pendingSourceFailure == "reference_source_changed"
        && runtime.requestedGeneration() == generation,
        "the actual prepared switch preserves audible C and publishes its confirmed change reason");
    overwrite (original); probe();
    for (int n = 0; n < 16 && c.snapshot().checkSelection->checkId != secondCheckId; ++n) feed();
    const auto after = c.snapshot();
    require (after.checkReady && after.checkSelection->checkId == secondCheckId
             && after.checkSelection->rejectionCode.isEmpty() && after.checkSelection->pendingSourceFailure.isEmpty()
             && runtime.requestedGeneration() == generation,
             "the same next Check becomes ready after reads recover without another choice");
    c.selectA(); feed();
}

inline void testReferenceInitialSwitchRetry (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    namespace revision = ref::revision_detail;
    for (int failure = 0; failure < 3; ++failure)
    {
        const auto root = sandbox.getChildFile ("initial-switch-retry-" + juce::String (failure));
        require (root.createDirectory().wasOk(), "isolated first verification fixture");
        publishTwoCheckLibrary (root, 2, true);
        ref::ReferenceComparisonController c (root);
        control::stopWorkers (c); c.configure ({"initial-switch-post", {}, 42, true}, 48000, 2);
        require (((juce::Thread&) control::sourceVisual (c, {})).stopThread (-1), "stop the visual owner");
        c.setPresented (true); control::service (c); c.observeTransport (0, true, true); control::service (c);
        auto& runtime = control::roleFor (c, {2});
        for (int role = 1; role <= 3; ++role)
            control::stopPageRefill (control::currentPagesFor (control::roleFor (c, {role}), {}));
        juce::AudioBuffer<float> block (2, 512);
        int cursor = 0;
        bool prepare = false;
        const auto feed = [&] {
            for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (block.getWritePointer (ch), 0.125f, 512);
            c.observeTransport (cursor, true, true); c.observeAInput (block, cursor, true, true, true);
            beginReferenceRtProbe();
            const bool rendered = c.renderSelectedB (block, cursor, true, true, true);
            require (endReferenceRtProbe() == 0, "first verification retries allocate nothing on the callback");
            cursor += 512; control::service (c);
            c.servicePendingAudition (prepare ? -25.0 : std::numeric_limits<double>::quiet_NaN(), -6, true);
            return rendered;
        };
        require (c.requestAudition (2, -22, -6) && feed() && feed(), "C renders before the new initial verification");
        const auto before = c.snapshot();
        control::probeSourceNow (*runtime.visualBinding().source);
        const auto file = root.getChildFile ("alternate.wav"), saved = root.getChildFile ("held.wav");
        if (failure == 0) require (file.moveFileTo (saved), "remove only the requested path for its first stat");
        else if (failure < 3) revision::verifyFailureForTest.store (failure);
        require (c.selectCheck (before.checkTargets[1].id), "choose the next C through the real entry");
        const auto generation = runtime.requestedGeneration();
        control::service (c);
        require (revision::service().waitForIdle (std::chrono::seconds (30)), "first verification completes");
        control::service (c);
        if (failure == 0) require (saved.moveFileTo (file), "restore the requested path after one failed stat");
        else if (failure < 3) require (revision::verifyFailureForTest.load() == 0, "the real verifier consumed the one-shot failure");
        require (control::preparedSourceFor (runtime, {}) == nullptr, "failure occurred before playback preparation");
        const auto deadline = control::sourceRetryDeadline (runtime, {});
        require (deadline > 0 && deadline <= juce::Time::currentTimeMillis() + 1000,
                 "a retained C retries source reads promptly and bounds failed copy attempts");
        for (int n = 0; n < 4; ++n)
        {
            require (feed() && std::abs (block.getSample (0, 511) - 0.25f) < 1.0e-7f,
                     "the first failed verification preserves the exact approved old C PCM");
            const auto held = c.snapshot().checkSelection;
            require (held->playbackIdentity == before.checkSelection->playbackIdentity
                     && std::abs (held->appliedGainDb - before.checkSelection->appliedGainDb) < 1.0e-9
                     && held->pendingSourceFailure.isEmpty(), "a brief failure keeps identity and gain without a notice");
        }
        control::sourceRetryDeadline (runtime, {}) = juce::Time::currentTimeMillis() - 1;
        prepare = true;
        for (int n = 0; n < 16 && c.snapshot().checkSelection->checkId == before.checkSelection->checkId; ++n)
        { feed(); require (revision::service().waitForIdle (std::chrono::seconds (30)), "recovery verification completes"); }
        require (c.snapshot().checkSelection->checkId != before.checkSelection->checkId
                 && c.snapshot().checkSelection->pendingSourceFailure.isEmpty()
                 && runtime.requestedGeneration() == generation, "the original switch completes after recovery without another choice");
        c.selectA(); feed();
    }
}

// A source whose earlier attempt ended in a read failure is chosen again after the disk returns:
// the first check of the new selection starts a fresh attempt at once instead of serving the old
// failure through a new reservation.
inline void testReferenceReselectionRetry (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile ("source-retry-reselect");
    require (root.createDirectory().wasOk(), "isolated reselection retry fixture");
    const auto base = makeWholeSongFixture (root, root.getChildFile ("input.wav"),
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333");
    const auto manifest = hypha::tests::version_formats::publish (root, base.audio, 48000, false, 0, 1);
    require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "publish reselection fixture");
    const auto file = root.getChildFile ("version-1.wav");
    require (file.setLastModificationTime (file.getLastModificationTime() - juce::RelativeTime::seconds (20)),
             "change only the source timestamp before the first load");
    const auto id = manifest["versions"][0]["entry_id"].toString();
    const auto selection = id + "/" + id + "/" + id;
    ref::ReferenceComparisonController c (root);
    control::stopWorkers (c); c.configure ({"reselect-post", {}, 42, true}, 48000, 2);
    require (((juce::Thread&) control::sourceVisual (c, {})).stopThread (-1), "stop the visual worker for a deterministic hash count");
    control::service (c);
    require (c.selectVersion (selection), "select the uncached source");
    auto& runtime = control::roleFor (c, { 1 });
    auto& service = ref::revision_detail::service();
    require (service.waitForIdle (std::chrono::seconds (30)), "no earlier hash is still running");
    struct Reset { ~Reset() { ref::revision_detail::persistentReadFailureForTest.store (false); } } reset;
    ref::revision_detail::persistentReadFailureForTest.store (true);
    control::service (c);
    require (service.waitForIdle (std::chrono::seconds (30)), "the first attempt finishes");
    control::service (c);
    require (control::sourceRetryDeadline (runtime, {}) > juce::Time::currentTimeMillis(),
             "the failed attempt reserves a retry");
    ref::revision_detail::persistentReadFailureForTest.store (false); // The disk returns.
    require (c.selectVersion (selection), "choose the same source again");
    const auto started = service.startedCount();
    control::service (c);
    require (control::sourceRetryDeadline (runtime, {}) == 0,
             "a new selection does not wait for the old failure's reservation");
    require (service.waitForIdle (std::chrono::seconds (30)) && service.startedCount() == started + 1,
             "the new selection starts one fresh attempt at once");
    juce::AudioBuffer<float> input (2, 256);
    for (int cursor = 0; cursor < 192000; cursor += 256)
    {
        for (int ch = 0; ch < 2; ++ch) input.copyFrom (ch, 0, base.audio, ch, cursor, 256);
        c.observeTransport (cursor, true, true); c.observeAInput (input, cursor, true, true, true);
        control::service (c);
    }
    require (c.snapshot().versionReady && c.snapshot().versionSelection->rejectionCode.isEmpty(),
             "the chosen-again source becomes ready without the old reservation");
}

// No prepared publication exists yet: an explicit press during a failed first copy
// must survive until the same requested selection is verified and can really play.
inline juce::String initialOpenFailurePath;
inline std::atomic<int> initialOpenFailures { 0 };
inline void failInitialSourceOpen (const ref::RuntimeSource& source)
{
    if (source.absolutePath != initialOpenFailurePath) return;
    ++initialOpenFailures;
    ref::revision_detail::threadOpenFailureForTest = true;
}
inline void testReferenceInitialCopyAudition (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    namespace revision = ref::revision_detail;
    for (int failure = 0; failure < 5; ++failure)
    for (const bool stopped : {false, true})
    for (const bool cancel : {false, true})
    {
        const auto root = sandbox.getChildFile ("initial-copy-" + juce::String (failure)
            + juce::String (stopped ? 1 : 0) + juce::String (cancel ? 1 : 0));
        require (root.createDirectory().wasOk(), "isolated initial copy fixture");
        publishTwoCheckLibrary (root, 2, true);
        ref::ReferenceComparisonController c (root);
        control::stopWorkers (c); c.configure ({"initial-copy-post", {}, 42, true}, 48000, 2);
        require (((juce::Thread&) control::sourceVisual (c, {})).stopThread (-1), "deterministic visual owner");
        struct Reset { ~Reset() {
            revision::snapshotFreeBytesForTest.store (-1); revision::snapshotWriteLimitForTest.store (-1);
            revision::persistentPathFailureForTest.store (false); revision::persistentReadFailureForTest.store (false);
            revision::sourceReaderOpenHookForTest.store (nullptr); revision::sourcePageOpenHookForTest.store (nullptr);
            revision::sourceInitialFormatOpenHookForTest.store (nullptr);
            revision::verifyFailureForTest.store (0);
        } } reset;
        if (failure == 0) revision::persistentPathFailureForTest.store (true);
        else if (failure == 1) revision::persistentReadFailureForTest.store (true);
        else
        {
            revision::snapshotFreeBytesForTest.store (128 * 1024 * 1024);
            initialOpenFailurePath = root.getChildFile ("source.wav").getFullPathName(); initialOpenFailures.store (0);
            if (failure == 2) revision::sourceReaderOpenHookForTest.store (&failInitialSourceOpen);
            else if (failure == 3) revision::sourcePageOpenHookForTest.store (&failInitialSourceOpen);
            else revision::sourceInitialFormatOpenHookForTest.store (&failInitialSourceOpen);
        }
        c.setPresented (true); control::service (c);
        require (revision::service().waitForIdle (std::chrono::seconds (30)), "initial verification finishes");
        control::service (c);
        auto& runtime = control::roleFor (c, {2});
        const auto failed = runtime.snapshot();
        const bool unpublished = !runtime.visualBinding().source && !c.snapshot().checkArmable;
        if (failed.state != ref::RuntimeState::waiting || !unpublished
            || failed.rejectionCode != (failure < 2 ? "reference_source_read_failed" : "reference_source_revalidating"))
            std::cerr << "initial open case=" << failure << " stopped=" << stopped << " cancel=" << cancel
                      << " state=" << static_cast<int> (failed.state) << " reason=" << failed.rejectionCode
                      << " identity=" << failed.playbackIdentity << " failures=" << initialOpenFailures.load() << '\n';
        require (failed.state == ref::RuntimeState::waiting && unpublished
            && failed.rejectionCode == (failure < 2 ? "reference_source_read_failed" : "reference_source_revalidating"),
            "initial source I/O failure waits before any playback publication");
        if (failure >= 2) require (initialOpenFailures.load() > 0, "the real page or decoder open boundary fails after verification");
        const auto generation = runtime.requestedGeneration();
        juce::AudioBuffer<float> block (2, 512);
        const auto feed = [&] (bool playing) {
            for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (block.getWritePointer (ch), 0.125f, 512);
            c.observeTransport (0, true, playing); c.observeAInput (block, 0, true, playing, true);
            beginReferenceRtProbe();
            const auto rendered = c.renderSelectedB (block, 0, true, true, true);
            require (endReferenceRtProbe() == 0, "initial-copy waiting allocates nothing on the audio callback");
            if (!rendered) for (int ch = 0; ch < 2; ++ch) for (int f = 0; f < 512; ++f)
                require (block.getSample (ch, f) == 0.125f, "an unverified initial source preserves every A sample");
            control::service (c); c.servicePendingAudition (-22, -6, playing); return rendered;
        };
        require (!feed (!stopped), "initial failure preserves A before the explicit press");
        require (!c.snapshot().checkArmable, "the initial failure precedes any armable playback identity");
        require (c.requestAudition (2, -22, -6),
                 "the real audition entry accepts a press during initial storage failure");
        for (int poll = 0; poll < 4; ++poll) require (!feed (!stopped), "failed source stays silent while the explicit choice waits");
        require (c.pendingSlot() == 2 && c.pendingAuditionNeedsService() && runtime.requestedGeneration() == generation,
                 "the pending audition belongs to the same requested selection generation");
        if (cancel) c.selectA();
        if (failure < 2) revision::snapshotFreeBytesForTest.store (-1);
        revision::snapshotWriteLimitForTest.store (-1);
        revision::persistentPathFailureForTest.store (false); revision::persistentReadFailureForTest.store (false);
        revision::sourceReaderOpenHookForTest.store (nullptr); revision::sourcePageOpenHookForTest.store (nullptr);
        revision::sourceInitialFormatOpenHookForTest.store (nullptr);
        control::sourceRetryDeadline (runtime, {}) = juce::Time::currentTimeMillis() - 1;
        bool rendered = false;
        for (int poll = 0; poll < 12; ++poll) rendered = feed (!stopped) || rendered;
        require (c.snapshot().checkReady && runtime.requestedGeneration() == generation, "the original source becomes ready after storage recovers");
        if (failure >= 2) require (!runtime.visualBinding().source->preparedAudio->snapshot->hasPrivateCopy(),
                                  "initial open recovery uses authenticated reads while capacity is still unavailable");
        if (stopped)
        {
            require (!rendered && !c.snapshot().bSelected, "storage recovery while stopped does not start output");
            for (int poll = 0; poll < 4; ++poll) rendered = feed (true) || rendered;
        }
        require (rendered == !cancel && c.snapshot().bSelected == !cancel,
                 "the held press starts automatically after recovery; A cancels it before recovery");
        if (!cancel) require (block.getSample (0, 511) == 0.25f, "the recovered source reaches actual matched PCM output");
        c.selectA();
    }
}
