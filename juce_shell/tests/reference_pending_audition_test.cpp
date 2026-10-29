#include "ReferenceControllerTestAccess.h"
#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"
#include "reference_rt_probe.h"
#include "ReferenceSelectionSafetyTest.h"
#include "ReferenceAutoVersionSafetyTest.h"
#include "TestPathBudget.h"
#include "ReferenceWholeSongInputClock.h"
void testReferencePendingAudition (const juce::File&);

static void testRepeatedVersionWait (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile ("queued-repeat");
    require (root.createDirectory(), "repeated Version fixture directory");
    const auto file = root.getChildFile ("source.wav");
    const auto fixture = makeWholeSongFixture (root, file,
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333", 2, false, true);
    const auto hash = juce::SHA256 (file).toHexString();
    auto preset = bindRuntimeV2WorkVersionPresetToSource (
        "88888888-8888-4888-8888-888888888888", "99999999-9999-4999-8999-999999999999",
        fixture.receipt, hash, fixture.pcmHash, "22222222-2222-4222-8222-222222222222",
        "33333333-3333-4333-8333-333333333333", fixture.audio.getNumSamples());
    auto* object = preset.getDynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0");
    object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    require (writeJson (root.getChildFile ("library/manifest.json"), independentLibraryManifest (root, preset, 1)),
             "repeated Version publication");
    ref::ReferenceComparisonController controller (root);
    controller.configure ({ "repeat-post", {}, 42, true }, 48000, 2);
    controller.setPresented (true);
    controller.observeTransport (0, true, false);
    control::await ([&] { return ! controller.snapshot().versions.empty(); }, "repeated Version arrives");
    require (controller.selectVersion (controller.snapshot().versions.front().id), "select repeated Version");
    int cursor = 0;
    WholeSongInputClock clock;
    std::cout << "repeated V: awaiting ambiguous passage" << std::endl;
    require (clock.waitFor (controller, fixture, cursor, [] (const auto& state) {
        return state.versionSelection && state.versionSelection->rejectionCode == "reference_alignment_ambiguous";
    }, 2, 15), "repeated passage reaches ambiguous alignment");
    std::cout << "repeated V: ambiguous passage established" << std::endl;
    require (controller.requestAudition (1, -14, -2), "V waits while the repeated passage is ambiguous");
    controller.servicePendingAudition (-14, -2, true);
    require (controller.snapshot().pendingAudition.waiting(), "the V intent stays pending");
    cursor = 48000 * 7;
    std::cout << "repeated V: awaiting distinct passage" << std::endl;
    const bool aligned = clock.waitFor (controller, fixture, cursor, [] (const auto& state) { return state.versionReady; }, 2, 15);
    if (! aligned)
    {
        const auto state = controller.snapshot();
        std::cerr << "repeated V: cursor=" << cursor << " pending=" << int (state.pendingAudition.stage)
                  << " reason=" << state.versionSelection->rejectionCode << " ready=" << state.versionReady << '\n';
    }
    require (aligned, "a distinct passage establishes Version alignment");
    std::cout << "repeated V: distinct passage aligned" << std::endl;
    controller.servicePendingAudition (-14, -2, true);
    juce::AudioBuffer<float> block (2, 256);
    const int position = juce::jmin (cursor, fixture.audio.getNumSamples() - 256);
    for (int channel = 0; channel < 2; ++channel)
        block.copyFrom (channel, 0, fixture.audio, channel, position, 256);
    controller.observeTransport (position, true, true);
    controller.observeAInput (block, position, true, true, true);
    require (controller.snapshot().audibleComparisonSlot == 1
        && controller.renderSelectedB (block, position, true, true, true),
        "queued V plays after the matching position is verified");
    std::cout << "repeated V: PASS after alignment" << std::endl;
}
bool runReferencePendingTests (int argc, char** argv, const juce::File&);
bool runReferencePendingTests (int argc, char** argv, const juce::File& sandbox)
{
    if (argc != 2 || juce::String (argv[1]) != "--pending-only") return false;
    testReferencePendingAudition (sandbox);
    require (hypha::tests::removeFixtureDirectory (sandbox), "pending fixture cleanup");
    return true;
}
void testReferencePendingAudition (const juce::File& sandbox)
{
    testRepeatedVersionWait (sandbox);
    verifyReferenceSelectionSafety (sandbox);
    verifyReferenceAutoVersionSafety (sandbox);
    using Stage = ref::PendingAuditionView::Stage;
    const auto root = sandbox.getChildFile ("queued-abc");
    require (root.createDirectory(), "queued audition fixture directory");
    const auto file = root.getChildFile ("song.wav");
    const auto fixture = makeWholeSongFixture (root, file,
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333");
    const auto hash = juce::SHA256 (file).toHexString();
    auto preset = bindRuntimeV2WorkVersionPresetToSource (
        "88888888-8888-4888-8888-888888888888", "99999999-9999-4999-8999-999999999999",
        fixture.receipt, hash, fixture.pcmHash, "22222222-2222-4222-8222-222222222222",
        "33333333-3333-4333-8333-333333333333", fixture.audio.getNumSamples());
    auto* object = preset.getDynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0");
    object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    auto check = preset["checks"][0].clone();
    check.getDynamicObject()->setProperty ("check_id", "44444444-4444-4444-8444-444444444444");
    check.getDynamicObject()->setProperty ("comparison_mode", "original");
    check["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    preset["checks"].getArray()->add (check);
    require (writeJson (root.getChildFile ("library/manifest.json"), independentLibraryManifest (root, preset, 1)),
        "queued source publication");
    ref::ReferenceComparisonController controller (root);
    const ref::RuntimeIdentity identity { "queued-post", {}, 42, true };
    controller.configure (identity, 48000, 2);
    controller.setPresented (true);
    juce::AudioBuffer<float> block (2, 256);
    const auto host = [&] (bool playing, bool valid = true, bool allowed = true)
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) block.setSample (c, i, 0.125f);
        beginReferenceRtProbe();
        controller.observeTransport (0, valid, playing);
        controller.observeAInput (block, 0, valid, playing, allowed);
        const bool rendered = controller.renderSelectedB (block, 0, valid, allowed, allowed);
        require (endReferenceRtProbe() == 0, "queued observation and output add zero RT allocations");
        if (!rendered) for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i)
            require (block.getSample (c, i) == 0.125f, "waiting keeps every A sample unchanged");
        return rendered;
    };
    const auto wait = [&] (const auto& condition)
    {
        if (hypha::tests::reference_control::waitUntil (std::chrono::steady_clock::now() + std::chrono::minutes (5), [&] { return condition (controller.snapshot()); })) return;
        const auto s = controller.snapshot();
        std::cerr << "queued state: " << s.rejectionCode << " / " << int(s.pendingAudition.stage) << '\n';
        require (false, "queued source preparation deadline");
    };
    host (false);
    wait ([] (const auto& s) { return s.checkArmable && !s.versions.empty(); });
    const auto bId = controller.snapshot().versions.front().id;
    require (controller.selectVersion (bId), "choose Version while stopped");
    wait ([] (const auto& s) { return s.versionArmable && s.checkArmable; });
    require (controller.requestAudition (2, -14, -2), "stopped C can be queued");
    controller.servicePendingAudition (-14, -2, false);
    require (controller.snapshot().pendingAudition.waiting() && !host (false)
        && !controller.snapshot().bSelected, "stopped queue is not audible selection");
    require (controller.selectVisualSlot (1) && controller.snapshot().pendingAudition.slot == 2,
        "display navigation preserves the C intent");
    controller.selectA(); host (true); controller.servicePendingAudition (-14, -2, true);
    require (!controller.pendingAuditionNeedsService() && !host (true), "A cancels before playback");
    host (false);
    require (controller.requestAudition (2, -14, -2), "queue C again");
    host (true);
    wait ([] (const auto& s) { return s.checkReady; });
    controller.servicePendingAudition (-14, -2, true);
    require (controller.snapshot().audibleComparisonSlot == 2 && host (true),
        "the first safe play starts queued C without an editor or another click");
    controller.selectA(); host (false);
    require (!controller.pendingAuditionNeedsService(), "one-shot intent does not rearm on stop");
    const bool cQueued = controller.requestAudition (2, -14, -2);
    if (!cQueued) { const auto s = controller.snapshot(); std::cerr << "C queue: armable=" << s.checkArmable
        << " playing=" << s.transportPlaying << " state=" << int(s.checkSelection->state)
        << " reason=" << s.checkSelection->rejectionCode << '\n'; }
    require (cQueued, "stopped C can replace an ended audition");
    const bool bQueued = controller.requestAudition (1, -14, -2);
    if (!bQueued) { const auto s = controller.snapshot(); std::cerr << "B queue: armable=" << s.versionArmable
        << " playing=" << s.transportPlaying << " reason=" << s.versionSelection->rejectionCode
        << " selected=" << s.selectedVersionId << " current=" << s.versionSelection->presetId << "/"
        << s.versionSelection->checkId << "/" << s.versionSelection->candidateId << '\n'; }
    require (bQueued, "stopped B replaces C's pending intent");
    require (controller.snapshot().pendingAudition.slot == 1 && !controller.snapshot().bSelected,
        "only the last explicitly queued source is armed");
    controller.setPresented (false);
    int cursor = 0;
    WholeSongInputClock clock;
    require (clock.waitFor (controller, fixture, cursor,
        [] (const auto& state) { return state.versionReady; }), "Version calibration worker completes");
    require (controller.snapshot().versionReady, "queued B still requires exact content alignment with its editor closed");
    controller.servicePendingAudition (-14, -2, true);
    require (controller.snapshot().audibleComparisonSlot == 1, "verified B starts once");
    controller.selectA(); host (false);
    require (controller.requestAudition (2, -14, -2), "queue C for a missing-clock boundary");
    host (true, false); controller.servicePendingAudition (-14, -2, true);
    require (!host (true, false) && controller.snapshot().pendingAudition.stage == Stage::checking,
        "an absent clock never starts the queued source");
    controller.servicePendingAudition (-14, -2, false);
    require (!controller.pendingAuditionNeedsService()
        && controller.snapshot().pendingAudition.stage == Stage::safetyChanged, "lost callback cancels with a reason");
    controller.selectA(); host (false);
    require (controller.requestAudition (2, -14, -2), "queue for bypass/offline boundary");
    host (true, true, false); controller.servicePendingAudition (-14, -2, true);
    require (!controller.pendingAuditionNeedsService() && !host (true), "offline/bypass cannot defer a surprise switch");
    host (false, true, false);
    require (controller.requestAudition (2, -14, -2), "queue while input is already forbidden");
    host (true, true, false); controller.servicePendingAudition (-14, -2, true);
    require (controller.snapshot().pendingAudition.stage == Stage::safetyChanged && !host (true),
        "already-forbidden input cannot masquerade as an unobserved first callback and switch later");
    host (false);
    require (controller.requestAudition (2, -14, -2), "queue before changing source controls");
    // 同じ Cue を選び直しても何も変わらない（押した C は待ったまま）。別の Cue にすると新しい選択で鳴らす
    // （ReferenceAbcvRoles の C の切替）。A を押すと待ちは消える。
    require (controller.selectCue (controller.snapshot().checkSelection->cueId)
        && controller.pendingAuditionNeedsService() && controller.snapshot().pendingAudition.slot == 2,
        "choosing the same Cue keeps the queued C");
    controller.selectA();
    require (!controller.pendingAuditionNeedsService(), "A cancels the queued C");
    require (controller.requestAudition (2, -14, -2), "queue before host restore");
    const auto saved = controller.savedSettings();
    controller.restoreSettings (saved);
    require (!controller.pendingAuditionNeedsService(), "reopen never restores permission to play");
    preset["checks"][1].getDynamicObject()->setProperty ("comparison_mode", "loudness_match");
    require (writeJson (root.getChildFile ("library/manifest.json"), independentLibraryManifest (root, preset, 2)),
        "publish a match-required C");
    wait ([] (const auto& s) { return s.checkArmable && s.checkSelection->comparisonMode == "loudness_match"; });
    require (controller.requestAudition (2, -1, -2), "queue loudness-matched C");
    host (true); wait ([] (const auto& s) { return s.checkReady; });
    controller.servicePendingAudition (std::numeric_limits<double>::quiet_NaN(), -2, true);
    require (controller.snapshot().pendingAudition.stage == Stage::level && !host (true),
        "unknown live A loudness cannot silently choose original C volume");
    controller.servicePendingAudition (-1, -2, true);
    require (controller.snapshot().pendingAudition.stage == Stage::ceilingExceeded && !host (true),
        "ceiling-limited MATCH cancels instead of falling back to original volume");
    controller.selectA(); host (false);
    preset["checks"][1].getDynamicObject()->setProperty ("comparison_mode", "original");
    require (writeJson (root.getChildFile ("library/manifest.json"), independentLibraryManifest (root, preset, 3)),
        "restore the fixture's explicit original mode");
    // 試聴コピーのサンプルレート変換は自動。承認を待たずに準備ができ、止まっていれば再生を待つ。
    controller.configure (identity, 44100, 2); host (false);
    wait ([] (const auto& s) { return s.checkArmable && s.checkReady; });
    require (controller.requestAudition (2, -14, -2), "C queues on a converted audition copy");
    controller.servicePendingAudition (-14, -2, false);
    require (controller.snapshot().pendingAudition.waiting() && !controller.snapshot().bSelected,
        "conversion alone never starts stopped audio");
    host (true); controller.servicePendingAudition (-14, -2, true);
    require (controller.snapshot().audibleComparisonSlot == 2, "the converted copy starts at the first safe play");
    controller.selectA(); host (false);
    // 止めても選んだまま。再生すると同じ音・同じ gain で戻り、MATCH は測り直さない。A を押すと戻らない。
    preset["checks"][1].getDynamicObject()->setProperty ("comparison_mode", "loudness_match");
    require (writeJson (root.getChildFile ("library/manifest.json"), independentLibraryManifest (root, preset, 4)),
        "publish a matched C for resume");
    wait ([] (const auto& s) { return s.checkArmable && s.checkSelection->comparisonMode == "loudness_match"; });
    host (true); wait ([] (const auto& s) { return s.checkReady; });
    namespace control = hypha::tests::reference_control;
    const auto eventDeadline = [] { return std::chrono::steady_clock::now() + std::chrono::minutes (5); };
    const auto allEventsDrained = [&] {
        for (int slot : { 1, 2, 3 }) if (! control::eventsFor (control::roleFor (controller, { slot }), {}).empty()) return false;
        return true;
    };
    require (control::waitUntil (eventDeadline(), allEventsDrained),
             "the actual A callback drains all earlier runtime event sessions before a new C starts");
    require (controller.requestAudition (2, -28, -12) && host (true), "matched C plays");
    const auto matched = controller.snapshot().checkSelection->appliedGainDb;
    const auto countEvents = [&] { return root.getChildFile ("library/events").findChildFiles (juce::File::findFiles, true, "*.json").size(); };
    auto& checkRuntime = control::roleFor (controller, { 2 });
    control::EventFact current;
    require (control::waitUntil (eventDeadline(), [&] {
        const auto sessions = control::eventsFor (checkRuntime, {});
        if (sessions.size() != 1 || ! sessions.front().startWritten) return false;
        current = sessions.front(); return true;
    }), "the real worker stores this C's one start before the event-count baseline");
    require (! current.returnRequested && current.startedAt > 0 && current.completedAt == 0,
             "the stored C session is open and has not requested a return");
    const auto eventFile = [&] (const juce::String& id) {
        return root.getChildFile ("library/events").getChildFile (current.instanceId).getChildFile (id + ".json");
    };
    const auto startBytes = eventFile (current.startedId).loadFileAsString();
    const auto start = juce::JSON::parse (startBytes);
    require (start["event_id"].toString() == current.startedId && start["run_id"].toString() == current.runId
                 && start["event_type"].toString() == "audition_started" && ! eventFile (current.completedId).exists(),
             "the immutable start belongs to the captured runtime session, without a completion");
    const auto eventsBeforePause = countEvents();

    require (std::abs (matched + 6.0) < 1.0e-9, "C matches A at -6 dB");
    const auto unknown = std::numeric_limits<double>::quiet_NaN();
    require (!host (false) && controller.snapshot().audibleComparisonSlot == 0 && controller.pendingAuditionNeedsService(),
        "stopping returns to A and keeps the choice");
    controller.servicePendingAudition (unknown, unknown, false);
    require (controller.snapshot().pendingAudition.waiting() && controller.snapshot().pendingAudition.slot == 2
        && !host (false), "the kept choice waits for playback without sounding");
    host (true); wait ([] (const auto& s) { return s.checkReady; });
    controller.servicePendingAudition (unknown, unknown, true);
    require (controller.snapshot().audibleComparisonSlot == 2 && host (true)
        && std::abs (controller.snapshot().checkSelection->appliedGainDb - matched) < 1.0e-12,
        "play resumes the same C at the same gain without measuring A again");
    const auto resumed = control::eventsFor (checkRuntime, {});
    require (resumed.size() == 1 && resumed.front().runId == current.runId
        && resumed.front().startedId == current.startedId && resumed.front().completedId == current.completedId
        && resumed.front().startedAt == current.startedAt && resumed.front().completedAt == 0
        && resumed.front().startWritten && ! resumed.front().returnRequested
        && ! eventFile (current.completedId).exists() && eventFile (current.startedId).loadFileAsString() == startBytes
        && countEvents() == eventsBeforePause, "automatic resume continues the same immutable audition event session");

    controller.selectA(); host (true); host (false); host (true);
    controller.servicePendingAudition (unknown, unknown, true);
    require (!controller.pendingAuditionNeedsService() && controller.snapshot().audibleComparisonSlot == 0 && !host (true),
        "after A the choice is not resumed");
    require (control::waitUntil (eventDeadline(), allEventsDrained),
             "the explicit A return drains the worker's completion before checking all event files");
    const auto completed = juce::JSON::parse (eventFile (current.completedId).loadFileAsString());
    require (completed["event_id"].toString() == current.completedId && completed["run_id"].toString() == current.runId
        && completed["event_type"].toString() == "audition_completed"
        && completed["payload"]["started_event_id"].toString() == current.startedId
        && countEvents() == eventsBeforePause + 1 && eventFile (current.startedId).loadFileAsString() == startBytes,
        "one explicit A return completes the original session once, with no delayed extra resume session");
    host (false);
    require (juce::SHA256 (file).toHexString() == hash, "all auditions preserve the OS source file");
    require (controller.requestAudition (2, -28, -12), "queue before source removal");
    const auto heldFile = file.getSiblingFile ("temporarily-unavailable.wav");
    require (file.moveFileTo (heldFile), "temporarily move only the disposable fixture source");
    wait ([] (const auto& s) { return s.checkSelection->state == ref::RuntimeState::waiting
                                   && s.checkSelection->rejectionCode == "reference_source_read_failed"; });
    controller.servicePendingAudition (-28, -12, false);
    require (controller.snapshot().pendingAudition.waiting()
        && controller.pendingAuditionNeedsService() && !host (false), "unavailable source keeps the pending choice silent");
    require (heldFile.moveFileTo (file), "restore the same fixture source");
    host (true); wait ([] (const auto& s) { return s.checkReady; });
    controller.servicePendingAudition (-28, -12, true);
    require (controller.snapshot().audibleComparisonSlot == 2 && host (true), "pending choice plays when source returns");
    std::cout << "Queued ABC: identity, readiness, cancel, restore, SRC, gain ceiling, missing source and RT safety PASS\n";
}
