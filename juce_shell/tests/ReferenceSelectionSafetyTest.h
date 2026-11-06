#pragma once
#include "ReferenceControllerTestAccess.h"
#include "../src/reference_audition/ReferenceLiveALevel.h"
#include "ReferenceWholeSongInputClock.h"
namespace hypha::tests::reference_control
{
struct TargetChoiceLockTag {};
juce::CriticalSection& targetChoiceLockFor (Comparison&, TargetChoiceLockTag);
template <auto Member> struct TargetChoiceLockAccess
{ friend juce::CriticalSection& targetChoiceLockFor (Comparison& c, TargetChoiceLockTag) { return c.*Member; } };
template struct TargetChoiceLockAccess<&Comparison::targetChoiceLock>;
}
namespace
{
void verifyCheckTargetRestoreCrossing (ref::ReferenceComparisonController& controller)
{
    namespace control = hypha::tests::reference_control;
    const auto original = controller.savedSettings();
    auto restored = original;
    ref::MeasuredTarget target;
    target.id = "target-abcdef"; target.name = "Saved target"; target.duration = 3;
    target.integrated = -24; target.maximumTruePeak = -6;
    target.spectrum.p10Db.assign (64, -60); target.spectrum.medianDb.assign (64, -50);
    target.spectrum.p90Db.assign (64, -40); target.spectrum.balanceDb.fill (-20);
    restored.targets.items = { target }; restored.targets.selected = target.id; restored.viewedSlot = 3;
    require (target.valid(), "restore race has a valid numeric target");
    const auto choices = controller.snapshot().checkSelection->checkTargets;
    const auto alternate = std::find_if (choices.begin(), choices.end(), [&] (const auto& choice) {
        return choice.id != original.check.checkId + "/" + original.check.candidateId;
    });
    require (alternate != choices.end(), "restore race has two real Checks");
    juce::WaitableEvent crossed;
    std::thread restore;
    controller.setBeforeCheckCommitForTest ([&] {
        restore = std::thread ([&] {
            auto& mutex = control::targetChoiceLockFor (controller, {});
            const bool entered = mutex.tryEnter();
            if (entered) { mutex.exit(); controller.restoreSettings (restored); }
            crossed.signal();
            if (! entered) controller.restoreSettings (restored);
        });
        require (crossed.wait (30000), "restore attempts to cross the real Check selection before numeric deselection");
    });
    const bool selected = controller.selectCheck (alternate->id);
    if (restore.joinable()) restore.join();
    controller.setBeforeCheckCommitForTest ({});
    const auto result = controller.savedSettings();
    require (selected && result.check.target() == restored.check.target()
        && result.targets.selected == target.id && result.viewedSlot == restored.viewedSlot,
        "restore after a Check transaction retains its Check, numeric selection and page together");
    require (controller.selectCheck (alternate->id) && controller.savedSettings().targets.selected.isEmpty(),
             "a subsequent explicit Check selection still clears the numeric selection");
    controller.restoreSettings (original);
}

void verifyReferenceSelectionSafety (const juce::File& sandbox)
{
    KirinObservatoryFrame frame {};
    frame.signal_state = KIRIN_SIGNAL_STATE_ACTIVE;
    frame.meter.state = KIRIN_METER_SESSION_ACTIVE;
    frame.meter.lufs_i = -28; frame.meter.max_true_peak = -12;
    const auto level = ref::liveALevel (frame, true, true, true);
    require (level.loudness == -28 && level.peak == -12, "control reads the live A meter, not a displayed/frozen comparison");
    require (!std::isfinite (ref::liveALevel (frame, false, true, true).loudness)
        && !std::isfinite (ref::liveALevel (frame, true, false, true).peak)
        && !std::isfinite (ref::liveALevel (frame, true, true, false).loudness),
        "missing poll, heartbeat or playback cannot reuse a displayed A level");
    frame.meter.state = KIRIN_METER_SESSION_EMPTY;
    require (!std::isfinite (ref::liveALevel (frame, true, true, true).loudness), "empty meter is not a level observation");
    frame.meter.state = KIRIN_METER_SESSION_ACTIVE; frame.signal_state = KIRIN_SIGNAL_STATE_INACTIVE;
    require (!std::isfinite (ref::liveALevel (frame, true, true, true).loudness), "inactive meter cannot lend an old level");

    const auto root = sandbox.getChildFile ("selection-safety");
    require (root.createDirectory(), "selection safety fixture directory");
    const auto file = root.getChildFile ("song.wav");
    const auto fixture = makeWholeSongFixture (root, file,
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333");
    auto preset = bindRuntimeV2WorkVersionPresetToSource (
        "88888888-8888-4888-8888-888888888888", "99999999-9999-4999-8999-999999999999",
        fixture.receipt, juce::SHA256 (file).toHexString(), fixture.pcmHash,
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333", fixture.audio.getNumSamples());
    auto* object = preset.getDynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0"); object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    auto check = preset["checks"][0].clone();
    check.getDynamicObject()->setProperty ("check_id", "44444444-4444-4444-8444-444444444444");
    check.getDynamicObject()->setProperty ("comparison_mode", "loudness_match");
    check["candidates"][0]["cues"][0].getDynamicObject()->setProperty ("loop_enabled", true);
    preset["checks"].getArray()->add (check);
    auto alternateCheck = check.clone();
    alternateCheck.getDynamicObject()->setProperty ("check_id", "55555555-5555-4555-8555-555555555555");
    preset["checks"].getArray()->add (alternateCheck);
    const auto publish = [&] (int revision) {
        require (writeJson (root.getChildFile ("library/manifest.json"), independentLibraryManifest (root, preset, revision)),
            "selection safety publication");
    };
    publish (1);
    ref::ReferenceComparisonController controller (root);
    controller.configure ({ "selection-safety-post", {}, 42, true }, 48000, 2);
    controller.setPresented (true);
    juce::AudioBuffer<float> block (2, 256);
    const auto host = [&] (bool playing) {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) block.setSample (c, i, 0.125f);
        beginReferenceRtProbe();
        controller.observeTransport (0, true, playing);
        controller.observeAInput (block, 0, true, playing, true);
        const bool rendered = controller.renderSelectedB (block, 0, true, true, true);
        require (endReferenceRtProbe() == 0, "selection fixes add no RT allocations");
        if (!rendered) for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i)
            require (block.getSample (c, i) == 0.125f, "denied selection keeps A bit identical");
        return rendered;
    };
    const auto wait = [&] (const auto& condition) {
        if (hypha::tests::reference_control::waitUntil (std::chrono::steady_clock::now() + std::chrono::minutes (5), [&] { return condition (controller.snapshot()); })) return;
        require (false, "selection safety preparation deadline");
    };
    host (false);
    wait ([] (const auto& s) { return s.checkArmable && !s.versions.empty(); });
    verifyCheckTargetRestoreCrossing (controller);
    // V の自動特定の AUTO は V の選択だけを替える：押して待っている C を消さない。V を選んだ後は AUTO が上書きしない。
    require (controller.requestAudition (2, level.loudness, level.peak), "queue C before AUTO");
    const auto autoId = controller.snapshot().versions.front().id;
    require (controller.selectVersion (autoId, true) && controller.pendingSlot() == 2
                 && controller.snapshot().selectedVersionId == autoId && controller.snapshot().versionAuto,
             "AUTO chooses V without touching the queued C");
    controller.selectA();
    require (controller.selectVersion (controller.snapshot().versions.front().id) && ! controller.snapshot().versionAuto,
             "choose safety Version");
    require (! controller.selectVersion (autoId, true), "AUTO never replaces a Version the user chose");
    wait ([] (const auto& s) { return s.versionArmable; });
    int cursor = 0;
    WholeSongInputClock clock;
    require (clock.waitFor (controller, fixture, cursor,
        [] (const auto& state) { return state.versionReady; }), "Version calibration worker completes");
    require (controller.snapshot().versionReady, "safety Version has verified content correspondence");
    require (controller.requestAudition (1, level.loudness, level.peak), "start safety Version B");
    const auto b = *controller.snapshot().versionSelection;
    require (!std::isfinite (b.aIntegratedLoudness), "Version's frozen display must not fabricate whole-song A LUFS");
    host (true);
    require (controller.requestAudition (2, level.loudness, level.peak), "B to C uses a new live A level observation");
    const auto fromB = *controller.snapshot().checkSelection;
    require (!fromB.comparisonFallbackOriginal && std::abs (fromB.appliedGainDb + 6) < 1e-9, "B to C applies exact -6 dB MATCH");
    controller.selectA(); for (int i = 0; i < 12; ++i) host (true);
    // 仕様 C：A の音量が何も無い（表示用の値でも NaN）あいだ、C は合わせずに押した選択を待たせる。鳴らさない。
    require (controller.requestAudition (2, b.aIntegratedLoudness, b.aMaximumTruePeakDbtp)
        && controller.snapshot().pendingAudition.waiting() && controller.snapshot().pendingAudition.slot == 2 && !host (true),
        "even accidental frozen display input cannot select unmatched C");
    controller.servicePendingAudition (b.aIntegratedLoudness, b.aMaximumTruePeakDbtp, true);
    require (controller.snapshot().pendingAudition.stage == ref::PendingAuditionView::Stage::level && !host (true),
        "C waits for an A level instead of playing unmatched");
    require (controller.requestAudition (2, -1, -2) && controller.snapshot().audibleComparisonSlot == 2
        && controller.snapshot().checkSelection->matchFailure == ref::MatchFailure::ceilingExceeded,
        "manual clicks play at the same ceiling as queued selection");
    require (host (true), "C actually renders before choosing a numeric target");
    auto& checkRuntime = hypha::tests::reference_control::roleFor (controller, { 2 });
    const auto offered = *controller.snapshot().checkSelection;
    const ref::LowerAOffer oldOffer { 2, offered.neededAttenuationDb, offered.playbackIdentity,
                                    offered.selectionGeneration, offered.matchFailureSerial };
    ref::MeasuredTarget data;
    data.id = "target-abc"; data.name = "Window"; data.duration = 2.0;
    data.setCenters();
    data.spectrum.p10Db = data.spectrum.medianDb = data.spectrum.p90Db = std::vector<float> (64, -40.0f);
    data.spectrum.balanceDb.fill (-40.0);
    require (controller.targetService().add (data) && controller.chooseTarget (data.id), "select numeric C while an old C offer exists");
    require (checkRuntime.returningToA(), "the real return fade retains C's output gate");
    require (controller.approveLowerAAndPlay (oldOffer) == ref::LowerAApproval::stale
        && controller.heldAttenuationDb() == 0.0 && controller.pendingSlot() != 2
        && controller.snapshot().checkSelection->matchFailureSerial != offered.matchFailureSerial,
        "numeric C invalidates the old approval before it can lower A or queue audio");
    require (! controller.requestAudition (2, -1, -2) && ! controller.selectC (-1, -2)
        && ! checkRuntime.selectB (-1, -2), "manual and final output admission reject audio for numeric C");
    controller.servicePendingAudition (-1, -2, true);
    for (int i = 0; i < 12; ++i) host (true);
    require (! controller.snapshot().bSelected && ! host (true) && controller.heldAttenuationDb() == 0.0,
             "servicing a stale C cannot start audio or attenuate A after the normal return fade");
    require (controller.chooseTarget ({}) && controller.approveLowerAAndPlay (oldOffer) == ref::LowerAApproval::stale,
             "switching back to the old C does not revive its old approval");
    require (controller.chooseTarget (data.id) && controller.targetService().remove (data.id), "remove the selected numeric target before the next C start");
    controller.selectA(); for (int i = 0; i < 12; ++i) host (true);
    require (controller.requestAudition (2, level.loudness, level.peak), "A to C uses the same control-plane level");
    require (controller.snapshot().checkSelection->appliedGainDb == fromB.appliedGainDb,
        "A to C and B to C have identical gain with the same live A observation");
    controller.selectA(); host (false);
    require (controller.requestAudition (2, -28, -12), "queue C before a label-only update");
    const auto originalKey = controller.snapshot().checkSelection->playbackIdentity;
    auto* cue = preset["checks"][1]["candidates"][0]["cues"][0].getDynamicObject();
    cue->setProperty ("label", "Renamed only"); publish (2);
    wait ([] (const auto& s) { return s.checkSelection->cueLabel == "Renamed only" && s.checkArmable; });
    controller.servicePendingAudition (-28, -12, false);
    require (controller.snapshot().pendingAudition.waiting()
        && controller.snapshot().checkSelection->playbackIdentity == originalKey, "cosmetic updates preserve the explicit pending intent");
    cue->setProperty ("loop_enabled", false); publish (3);
    wait ([&] (const auto& s) { return s.checkArmable && s.checkSelection->playbackIdentity != originalKey; });
    controller.servicePendingAudition (-28, -12, false);
    require (!controller.pendingAuditionNeedsService()
        && controller.snapshot().pendingAudition.stage == ref::PendingAuditionView::Stage::sourceChanged,
        "same Cue ID with different loop policy cancels pending authority with its reason");
    host (true); controller.servicePendingAudition (-28, -12, true);
    require (!controller.snapshot().bSelected && !host (true), "changed Cue never starts later from the old intent");
    require (controller.pendingSlot() == 0 && controller.snapshot().pendingAudition.slot == 2,
             "the completed C failure remains visible after its active intent ends");
    require (controller.targetService().add (data) && controller.chooseTarget (data.id),
             "select numeric C after the queued C failed");
    require (controller.snapshot().pendingAudition.stage == ref::PendingAuditionView::Stage::none
        && controller.snapshot().pendingAudition.reasonCode.isEmpty(),
        "a numeric target clears the old C failure and its recovery request");
    require (controller.chooseTarget ({}), "return to audio C after the numeric target");
    host (false);
    require (controller.requestAudition (1, -28, -12) && controller.pendingSlot() == 1,
             "queue V before choosing numeric C");
    const auto pendingVersion = controller.snapshot().pendingAudition;
    require (controller.chooseTarget (data.id) && controller.pendingSlot() == 1
        && controller.snapshot().pendingAudition.stage == pendingVersion.stage
        && controller.snapshot().pendingAudition.reasonCode == pendingVersion.reasonCode,
        "choosing numeric C preserves V's valid pending audition");
    controller.selectA();
    require (controller.chooseTarget ({}) && controller.targetService().remove (data.id), "restore audio C for subsequent selection checks");

    // Check the activation boundary too: a stale condition must not be revived by a fresh generation.
    ref::RuntimeV2Controller runtime (root);
    runtime.configure ({ "selection-safety-direct", {}, 42, true }, 48000, 2);
    runtime.observeTransport (0, true, true);
    hypha::tests::reference_control::await ([&] { return ! (!runtime.snapshot().auditionBuffered); });
    require (!runtime.selectB (-28, -12, runtime.normalSelectionTicket(), originalKey)
        && !runtime.snapshot().bSelected, "activation rechecks the pinned complete condition, not only a fresh generation");
    const auto current = runtime.snapshot();
    require (runtime.selectB (-28, -12, runtime.normalSelectionTicket(), current.playbackIdentity),
        "the newly selected exact condition can start");
    runtime.selectA();
    auto missingSummary = fixture.source.clone();
    missingSummary["measurement"].getDynamicObject()->setProperty ("summary", juce::var());
    const auto missingReceipt = stageWholeSongArtifact (root, "sources", missingSummary);
    auto* sourceArtifact = preset["checks"][1]["candidates"][0]["source_artifact"].getDynamicObject();
    sourceArtifact->setProperty ("relative_path", missingReceipt.relativePath);
    sourceArtifact->setProperty ("sha256", missingReceipt.sha256); sourceArtifact->setProperty ("bytes", missingReceipt.bytes);
    publish (4);
    hypha::tests::reference_control::await ([&] { return ! (runtime.snapshot().manifestRevision != 4); });
    require (runtime.snapshot().manifestRevision == 4 && !runtime.selectB (-28, -12)
        && runtime.snapshot().matchFailure == ref::MatchFailure::sourceLevelUnavailable,
        "missing source measurements cannot silently change manual MATCH to original");
    host (false); wait ([] (const auto& s) { return s.checkSelection->manifestRevision == 4 && s.checkArmable; });
    require (controller.requestAudition (2, -28, -12), "explicit queue for a source missing level evidence");
    host (true); controller.servicePendingAudition (-28, -12, true);
    require (controller.snapshot().pendingAudition.stage == ref::PendingAuditionView::Stage::sourceLevelUnavailable
        && !host (true), "queued missing source levels preserve a specific reason and unchanged A");
    sourceArtifact->setProperty ("relative_path", fixture.receipt.relativePath);
    sourceArtifact->setProperty ("sha256", fixture.receipt.sha256); sourceArtifact->setProperty ("bytes", fixture.receipt.bytes);
    preset["checks"][1].getDynamicObject()->setProperty ("comparison_mode", "peak_match"); publish (5);
    hypha::tests::reference_control::await ([&] { return ! (runtime.snapshot().manifestRevision != 5); });
    require (runtime.snapshot().manifestRevision == 5 && !runtime.selectB (-28, std::numeric_limits<double>::quiet_NaN())
        && runtime.snapshot().matchFailure == ref::MatchFailure::liveLevelUnavailable,
        "peak MATCH requires a measured live peak too");
    require (runtime.selectB (std::numeric_limits<double>::quiet_NaN(), -12)
        && std::abs (runtime.snapshot().appliedGainDb + 6) < 1e-9,
        "peak MATCH uses peak evidence independently of unavailable whole-song LUFS");
    runtime.selectA();
    std::cout << "Reference selection safety: live level, B/C path parity, strict MATCH, complete Cue identity PASS\n";
}
}
