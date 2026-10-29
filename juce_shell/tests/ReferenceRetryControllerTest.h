#pragma once
#include "ReferenceCheckSwitchTest.h"

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

// During a prepare-before-return Check switch, the next source's timestamp changes and its hash
// read keeps failing: the preparation hands the failure to the retry reservation instead of
// waiting forever, the playing C returns to A, and the same selection becomes ready again once
// reads recover, without another choice.
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
    const auto before = juce::Time::currentTimeMillis();
    control::service (c);
    require (service.waitForIdle (std::chrono::seconds (30)), "the preparation's hash attempt finishes");
    control::service (c);
    require (control::sourceRetryDeadline (runtime, {}) >= before + 5000,
             "a read failure during preparation reserves the retry clock");
    bool rendered = true;
    for (int n = 0; n < 4 && rendered; ++n) rendered = feed();
    require (!rendered && c.snapshot().audibleComparisonSlot == 0, "the unverifiable next Check returns to A");
    require (c.snapshot().checkSelection->state == ref::RuntimeState::waiting
             && c.snapshot().checkSelection->rejectionCode == "reference_source_read_failed"
             && runtime.requestedGeneration() == generation, "the same selection waits without a new choice");
    const auto started = service.startedCount();
    for (int n = 0; n < 8; ++n) feed();
    require (service.startedCount() == started, "polls during the reservation start no hash");
    ref::revision_detail::persistentReadFailureForTest.store (false);
    levelAvailable = true;
    control::sourceRetryDeadline (runtime, {}) = juce::Time::currentTimeMillis() - 1;
    control::service (c);
    require (service.waitForIdle (std::chrono::seconds (30)), "the retry attempt finishes");
    for (int n = 0; n < 16 && !c.snapshot().checkReady; ++n) feed();
    const auto after = c.snapshot();
    require (after.checkReady && after.checkSelection->checkId == secondCheckId
             && after.checkSelection->rejectionCode.isEmpty() && runtime.requestedGeneration() == generation,
             "the same next Check becomes ready after reads recover without another choice");
    c.selectA(); feed();
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
