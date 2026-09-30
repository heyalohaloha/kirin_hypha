#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"
#include "reference_rt_probe.h"

void testReferencePendingAudition (const juce::File&);
bool runReferencePendingTests (int argc, char** argv, const juce::File&);
bool runReferencePendingTests (int argc, char** argv, const juce::File& sandbox)
{
    if (argc != 2 || juce::String (argv[1]) != "--pending-only") return false;
    testReferencePendingAudition (sandbox);
    require (sandbox.deleteRecursively(), "pending fixture cleanup");
    return true;
}
void testReferencePendingAudition (const juce::File& sandbox)
{
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
        for (int i = 0; i < 1500; ++i)
        { if (condition (controller.snapshot())) return; juce::Thread::sleep (10); }
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
        << " reason=" << s.checkSelection->rejectionCode << " capture=" << s.captureAccess->busy() << '\n'; }
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
    for (int i = 0; i < 1500 && !controller.snapshot().versionReady; ++i)
    { observeWholeSongFixture (controller, fixture, cursor); juce::Thread::sleep (10); }
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
    require (controller.selectCue (controller.snapshot().checkSelection->cueId)
        && !controller.pendingAuditionNeedsService(), "changing Cue cancels the pending source");
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
    require (controller.snapshot().pendingAudition.stage == Stage::startFailed && !host (true),
        "ceiling-limited MATCH cancels instead of falling back to original volume");
    controller.selectA(); host (false);
    preset["checks"][1].getDynamicObject()->setProperty ("comparison_mode", "original");
    require (writeJson (root.getChildFile ("library/manifest.json"), independentLibraryManifest (root, preset, 3)),
        "restore the fixture's explicit original mode");
    controller.configure (identity, 44100, 2); host (false);
    wait ([] (const auto& s) { return s.checkArmable && s.checkSelection->sampleRateApprovalRequired; });
    require (controller.requestAudition (2, -14, -2), "C may wait for explicit SRC approval");
    controller.servicePendingAudition (-14, -2, false);
    require (controller.snapshot().pendingAudition.stage == Stage::approval && !host (false),
        "queued C cannot bypass sample-rate consent");
    require (controller.approveSampleRateConversion (2), "approve only the audition copy");
    wait ([] (const auto& s) { return !s.checkSelection->sampleRateApprovalRequired && s.checkReady; });
    controller.servicePendingAudition (-14, -2, false);
    require (controller.snapshot().pendingAudition.waiting() && !controller.snapshot().bSelected,
        "approval alone does not start stopped audio");
    host (true); controller.servicePendingAudition (-14, -2, true);
    require (controller.snapshot().audibleComparisonSlot == 2, "approval retains the same explicit queued identity");
    controller.selectA(); host (false);
    require (juce::SHA256 (file).toHexString() == hash, "all auditions preserve the OS source file");
    require (controller.requestAudition (2, -14, -2), "queue before source removal");
    require (file.deleteFile(), "remove only the disposable fixture source");
    wait ([] (const auto& s) { return s.checkSelection->state == ref::RuntimeState::rejected; });
    controller.servicePendingAudition (-14, -2, false);
    require (controller.snapshot().pendingAudition.stage == Stage::sourceChanged
        && !controller.pendingAuditionNeedsService() && !host (true), "missing source cancels with a retained reason");
    std::cout << "Queued ABC: identity, readiness, cancel, restore, SRC, gain ceiling, missing source and RT safety PASS\n";
}
