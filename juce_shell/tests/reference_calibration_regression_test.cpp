#include "reference_runtime_v2_analysis_test_support.h"
#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"

#include <cmath>

namespace
{
juce::var calibrationReceipt (ref::ReferenceComparisonController& controller, const WholeSongFixture& fixture,
                              const juce::File& root, int host = 0)
{
    controller.observeTransport (host, true, true);
    juce::Thread::sleep (100);
    const auto before = root.getChildFile ("library/events").findChildFiles (juce::File::findFiles, true, "*.json");
    require (controller.startBlind (-22, -6), "calibrated Version Blind starts");
    juce::AudioBuffer<float> input (2, 1024);
    for (int c = 0; c < 2; ++c) input.copyFrom (c, 0, fixture.audio, c, 0, 1024);
    require (controller.renderSelectedB (input, host, true, true, true), "confirm calibration receipt on actual audio callback");
    juce::var trial;
    for (int i = 0; i < 200 && trial.isVoid(); ++i)
    {
        controller.observeTransport (host, true, true); juce::Thread::sleep (10);
        for (const auto& file : root.getChildFile ("library/events").findChildFiles (juce::File::findFiles, true, "*.json"))
            if (!before.contains (file))
            {
                const auto event = juce::JSON::parse (file);
                if (event["event_type"] == "blind_compare_started") trial = event["payload"]["trial_start"];
            }
    }
    require (!trial.isVoid(), "read immutable accepted calibration");
    require (! controller.aInputFeeding(), "A is not handed to the observation while the Version Blind runs");
    controller.observeTransport (0, false, false); controller.endBlind();
    // 2026-10-04：終了の時点では V がまだ出力を持つ（A へ戻す途中）。そのあいだも、戻し終えた後も、
    // A は観測へ戻る（戻らないと V の画面が空、B・C の A の値が止まる）。
    require (controller.aInputFeeding(), "A goes back to the observation as soon as the Version Blind is ended");
    juce::Thread::sleep (100);
    controller.observeAInput (input, 0, false, false, true);
    bool drained = false;
    for (int i = 0; i < 500; ++i)
    {
        controller.observeTransport (0, false, false); juce::Thread::sleep (10);
        const auto state = controller.snapshot();
        if (!state.aCaptureAvailable && state.blindPhase == ref::BlindPhase::inactive) { drained = true; break; }
    }
    require (drained, "paused capture discontinuity is consumed before the next exact four-second observation");
    require (controller.aInputFeeding(), "A stays with the observation after the Version Blind has returned its output");
    return trial;
}

void feedPassage (ref::ReferenceComparisonController& controller, const WholeSongFixture& fixture,
                  int sourceStart, int hostStart, float gain, const char* label,
                  bool duringBlind = false)
{
    juce::AudioBuffer<float> input (2, 256);
    bool observedNewCapture = false;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        for (int frame = 0; frame < 192000; frame += 256)
        {
            for (int c = 0; c < 2; ++c)
                input.copyFrom (c, 0, fixture.audio, c, sourceStart + frame, 256);
            input.applyGain (gain);
            controller.observeTransport (hostStart + frame, true, true);
            controller.observeAInput (input, hostStart + frame, true, true, true);
            if ((frame % 8192) == 0)
                observedNewCapture = observedNewCapture
                    || controller.snapshot().aCaptureAvailable;
            juce::Thread::sleep (8);
        }
        for (int i = 0; i < 500; ++i)
        {
            controller.observeTransport (hostStart + 191744, true, true);
            juce::Thread::sleep (10);
            const auto state = controller.snapshot();
            observedNewCapture = observedNewCapture || state.aCaptureAvailable;
            if (duringBlind ? state.blindPhase == ref::BlindPhase::invalidated
                            : observedNewCapture && state.versionReady)
                return;
        }
        const auto state = controller.snapshot();
        std::cerr << "calibration observation retry label=" << label
                  << " attempt=" << (attempt + 1)
                  << " capture=" << state.aCaptureAvailable
                  << " capture_seen=" << observedNewCapture
                  << " version_ready=" << state.versionReady
                  << " gain_db=" << state.appliedGainDb
                  << " blind_phase=" << static_cast<int> (state.blindPhase)
                  << " rejection=" << state.rejectionCode << '\n';
    }
    require (false, "new observation must finish acoustic calibration");
}

void observationBoundaries()
{
    auto old = std::make_shared<ref::RuntimeACaptureAudio>();
    old->sampleRateHz = 48000; old->channels = 2; old->frameCount = 192000; old->cuePcmSha256 = "same";
    old->interleaved.resize (384000, 0.1f);
    ref::CalibrationObservation observations; observations.remember (old, 0, 48000);
    auto next = *old; next.startSample = 48000;
    require (ref::referenceObservationIdentity (*old) != ref::referenceObservationIdentity (next), "same PCM at another host position is a different observation");
    require (!observations.changed (next, 0, 48000), "unchanged repeated passage retains fixed calibration");
    for (auto& value : next.interleaved) value += 1.0e-6f;
    require (!observations.changed (next, 0, 48000), "dither does not cause gain chasing");
    for (auto& value : next.interleaved) value = 0.05f;
    require (observations.changed (next, 0, 48000), "gain edits in a corresponding passage invalidate calibration");
    require (!observations.changed (next, 192000, 48000), "different musical positions do not invent a gain edit");
    require (observations.changed (next, 44100, 44100) == false, "different source rate does not compare incompatible evidence");
    observations.clear(); require (!observations.changed (next, 0, 48000), "reconfiguration clears prior observations");
}

// 較正の済んだ V の試聴の道具（Kirin OS の 1 つの Version の公開・位置合わせ・音量の較正まで）。
std::unique_ptr<ref::ReferenceComparisonController> calibratedController (const juce::File& root, WholeSongFixture& fixture)
{
    require (root.createDirectory().wasOk(), "calibration fixture directory");
    const auto file = root.getChildFile ("version.wav");
    const juce::String recording = "22222222-2222-4222-8222-222222222222", version = "33333333-3333-4333-8333-333333333333";
    fixture = makeWholeSongFixture (root, file, recording, version);
    auto preset = bindRuntimeV2WorkVersionPresetToSource ("88888888-8888-4888-8888-888888888888",
        "99999999-9999-4999-8999-999999999999", fixture.receipt, juce::SHA256 (file).toHexString(), fixture.pcmHash,
        recording, version, fixture.audio.getNumSamples());
    auto* p = preset.getDynamicObject(); p->setProperty ("format", "kirin_hypha_reference_library_preset");
    p->setProperty ("version", "1.0"); p->removeProperty ("work_id"); p->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    libraryManifest (root, preset, 1);
    ref::ReferenceComparisonSettings legacy; legacy.viewedSlot = 1;
    legacy.version = { preset["source_template_artifact"]["preset_id"].toString(), preset["checks"][0]["check_id"].toString(),
        preset["checks"][0]["candidates"][0]["candidate_id"].toString(), preset["checks"][0]["candidates"][0]["default_cue_id"].toString() };
    require (writeJson (root.getChildFile ("library/manifest.json"), independentLibraryManifest (root, preset, 2)), "independent measured Version catalog");
    auto owned = std::make_unique<ref::ReferenceComparisonController> (root);
    auto& controller = *owned;
    controller.restoreSettings (legacy);
    controller.configure ({ "calibration-post", {}, 42, true }, 48000, 2);
    controller.setPresented (true);
    for (int i = 0; i < 500 && controller.snapshot().versions.empty(); ++i) juce::Thread::sleep (10);
    require (controller.snapshot().versions.size() == 1 && controller.snapshot().checkSelection->checkTargets.empty(),
             "B remains selectable without any C checks");
    require (!controller.snapshot().bSelected, "legacy restoration never auto-auditions");
    int position = 0;
    for (int i = 0; i < 1200 && !controller.snapshot().versionReady; ++i)
    { observeWholeSongFixture (controller, fixture, position); juce::Thread::sleep (10); }
    if (!controller.snapshot().versionReady) {
        const auto state = controller.snapshot();
        std::cerr << "migration/calibration state: " << state.selectedVersionId << " / " << state.presetId << "/" << state.checkId << "/" << state.candidateId
            << " from " << state.migratedVersionChoice << " reason " << state.rejectionCode << " capture " << state.aCaptureAvailable << '\n';
    }
    require (controller.snapshot().versionReady, "initial position and gain calibration");
    return owned;
}

// DAW の状態の読み込みで終わった VERSION BLIND：V が出力を返すまではほかの Blind を断り、返したら A を観測へ戻す。
void versionBlindReloadEnds (const juce::File& sandbox)
{
    WholeSongFixture fixture;
    auto owned = calibratedController (sandbox.getChildFile ("calibration-reload"), fixture);
    auto& controller = *owned;
    controller.observeTransport (0, true, true);
    juce::Thread::sleep (100);
    require (controller.startBlind (-22, -6), "a Version Blind that a state reload ends");
    juce::AudioBuffer<float> input (2, 1024);
    for (int c = 0; c < 2; ++c) input.copyFrom (c, 0, fixture.audio, c, 0, 1024);
    require (controller.renderSelectedB (input, 0, true, true, true), "the reloaded trial is heard");
    controller.restoreSettings (controller.savedSettings());
    require (! controller.reserveLocalBlind(), "another Blind waits until V has returned its output");
    for (int i = 0; i < 500 && (controller.snapshot().blindPhase != ref::BlindPhase::inactive || ! controller.aInputFeeding()); ++i)
    { controller.observeTransport (0, false, false); controller.servicePendingAudition (-23.0, -6.0, false); juce::Thread::sleep (10); }
    require (controller.snapshot().blindPhase == ref::BlindPhase::inactive && controller.aInputFeeding(),
             "A goes back to the observation when a state reload ends the Version Blind");
    require (controller.reserveLocalBlind(), "the Blind slot is free once V has returned its output");
    controller.releaseLocalBlind (0);
}

// 静かな B の曲（A を下げないと合わない）を、V の道具の B セットに置く（manifest の revision 2 に続く sets）。
void writeQuietBSet (const juce::File& root)
{
    const auto file = root.getChildFile ("quiet-b.wav");
    {
        juce::WavAudioFormat format;
        auto stream = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.release(), 48'000, 2, 32, {}, 0));
        juce::AudioBuffer<float> data (2, 96'000);
        for (int channel = 0; channel < 2; ++channel) juce::FloatVectorOperations::fill (data.getWritePointer (channel), 0.1f, 96'000);
        require (writer && writer->writeFromAudioSampleBuffer (data, 0, 96'000), "quiet B song samples");
    }
    const auto hash = juce::SHA256 (file).toHexString();
    const auto pcm = juce::String::repeatedString ("5", 64);
    auto runtimeSource = makeRuntimeV2Source (file, hash, pcm);
    addRuntimeV2MeasurementSummary (runtimeSource, -18.0, -2.0);
    const auto source = stageRuntimeV2Artifact (root, "sources", runtimeSource);
    auto* identity = new juce::DynamicObject();
    identity->setProperty ("catalog_reference_id", "catalog:source-test");
    identity->setProperty ("sha256_file", hash); identity->setProperty ("sha256_pcm", pcm);
    auto* artifact = new juce::DynamicObject();
    artifact->setProperty ("relative_path", source.relativePath);
    artifact->setProperty ("sha256", source.sha256); artifact->setProperty ("bytes", source.bytes);
    auto* cue = new juce::DynamicObject();
    cue->setProperty ("cue_id", "45454545-4545-4545-8545-454545454545"); cue->setProperty ("label", "Full track");
    cue->setProperty ("sample_rate_hz", 48'000); cue->setProperty ("start_sample", 0);
    cue->setProperty ("end_sample", 96'000); cue->setProperty ("loop_enabled", true);
    auto* song = new juce::DynamicObject();
    song->setProperty ("candidate_id", "56565656-5656-4656-8656-565656565656");
    song->setProperty ("display_name", "Quiet ref"); song->setProperty ("source_kind", "catalog_track");
    song->setProperty ("source_identity", juce::var (identity)); song->setProperty ("source_artifact", juce::var (artifact));
    song->setProperty ("cues", juce::Array<juce::var> { juce::var (cue) });
    song->setProperty ("default_cue_id", "45454545-4545-4545-8545-454545454545");
    song->setProperty ("preparation_status", "prepared");
    auto* set = new juce::DynamicObject();
    set->setProperty ("song_set_id", "67676767-6767-4767-8767-676767676767");
    set->setProperty ("revision_id", "78787878-7878-4878-8878-787878787878");
    set->setProperty ("rank", 1); set->setProperty ("name", "Quiet refs");
    set->setProperty ("songs", juce::Array<juce::var> { juce::var (song) });
    auto* sets = new juce::DynamicObject();
    sets->setProperty ("format", "kirin_hypha_reference_library_sets"); sets->setProperty ("version", "1.0");
    sets->setProperty ("revision", 1); sets->setProperty ("manifest_revision", 2);
    sets->setProperty ("song_sets", juce::Array<juce::var> { juce::var (set) });
    sets->setProperty ("check_sets", juce::Array<juce::var>()); sets->setProperty ("source_ranges", juce::Array<juce::var>());
    require (writeJson (root.getChildFile ("library/sets.json"), juce::var (sets)), "a B set with one quiet song");
}

// A を承認して下げたまま、VERSION BLIND は始めない（R-12・INV-S47：POST を二重に下げない）。断っても何も変えない。
// RETURN の後は始められる。
void versionBlindWaitsForReturn (const juce::File& sandbox)
{
    using hypha::output_owner::Activity;
    const auto root = sandbox.getChildFile ("calibration-lowered");
    require (root.getChildFile ("library").createDirectory().wasOk(), "lowered A fixture directory");
    writeQuietBSet (root);
    WholeSongFixture fixture;
    auto owned = calibratedController (root, fixture);
    auto& controller = *owned;
    int position = 0;
    juce::AudioBuffer<float> output (2, 1024);
    const auto host = [&]
    {
        if (position + 1024 > fixture.audio.getNumSamples()) position = 0;
        const auto at = position;
        for (int c = 0; c < 2; ++c) output.copyFrom (c, 0, fixture.audio, c, at, 1024);
        observeWholeSongFixture (controller, fixture, position);
        controller.renderSelectedB (output, at, true, true, true);
    };
    for (int i = 0; i < 1500 && ! controller.snapshot().referenceReady; ++i) { host(); juce::Thread::sleep (10); }
    require (controller.snapshot().referenceReady && controller.snapshot().versionReady, "the quiet B song prepares next to V");
    require (! controller.requestAudition (3, -10.0, -12.0), "a B MATCH over the ceiling does not play B");
    const auto needed = controller.snapshot().referenceSelection->neededAttenuationDb;
    require (needed < 0.0 && controller.approveLowerAAndPlay (3, needed) && controller.heldAttenuationDb() < 0.0,
             "the approval lowers A for B");
    require (! controller.startBlind (-22, -6) && ! controller.approveBlindLowerAAndStart (-22, -6),
             "VERSION BLIND does not start while A is lowered (POST is never lowered twice)");
    const auto refused = controller.outputDecision (Activity::versionBlind);
    require (refused.refused() && refused.reason == hypha::output_owner::Reason::returnFirst, "the refusal asks for RETURN first");
    require (controller.snapshot().blindPhase == ref::BlindPhase::inactive && controller.aInputFeeding()
                 && controller.reserveLocalBlind() == false,
             "a refused VERSION BLIND leaves A observed and keeps other Blinds waiting for RETURN too");
    controller.returnAToNormalLevel();
    for (int i = 0; i < 300 && controller.outputDecision (Activity::versionBlind).refused(); ++i) host();
    require (! controller.outputDecision (Activity::versionBlind).refused(), "after RETURN, VERSION BLIND may start");
}
}

void testReferenceCalibrationRegressions (const juce::File& sandbox);
void testReferenceCalibrationRegressions (const juce::File& sandbox)
{
    observationBoundaries();
    const auto root = sandbox.getChildFile ("calibration-regressions");
    WholeSongFixture fixture;
    auto owned = calibratedController (root, fixture);
    auto& controller = *owned;
    require (controller.savedSettings().version.target() == controller.snapshot().versions[0].id,
             "legacy C-based B choice migrates through its immutable receipt after C removal");
    const auto initial = calibrationReceipt (controller, fixture, root);
    const int a = static_cast<int> (initial["calibration"]["a"]["start_sample"]);
    const int b = static_cast<int> (initial["calibration"]["b"]["start_sample"]);
    feedPassage (controller, fixture, b, a, 0.5f, "gain-edit");
    require (controller.selectB (-28.0206, -12.0206), "B is available after repeated-passage gain edit");
    const auto corrected = controller.snapshot().appliedGainDb;
    require (std::abs (corrected + 6.0205999) < 0.01, "B gain follows the revised A calibration, not the old 0 dB receipt");
    // REF を離れる・窓を閉じるときも endBlind が呼ばれる。Blind が無ければ、聴いている V を止めない。
    controller.endBlind();
    require (controller.snapshot().bSelected, "leaving REF or closing the window keeps the V audition");
    controller.selectA();
    const auto gained = calibrationReceipt (controller, fixture, root);
    const auto hash = gained["sources"]["a"]["observation_pcm_sha256"].toString();
    const int gainedA = static_cast<int> (gained["calibration"]["a"]["start_sample"]);
    const int gainedB = static_cast<int> (gained["calibration"]["b"]["start_sample"]);
    feedPassage (controller, fixture, gainedB, gainedA + 48000, 0.5f, "relocation");
    const auto moved = calibrationReceipt (controller, fixture, root, gainedA + 48000);
    require (moved["sources"]["a"]["observation_pcm_sha256"] == hash, "relocation reuses exactly the same PCM hash");
    require (static_cast<int> (moved["calibration"]["a"]["start_sample"]) == gainedA + 48000
        && static_cast<int> (moved["calibration"]["b"]["start_sample"]) == gainedB,
        "identical PCM moved by one second receives a new host/source map");
    controller.observeTransport (gainedA + 48000, true, true); juce::Thread::sleep (100);
    require (controller.startBlind (-28, -12), "start trial before another live A edit");
    juce::AudioBuffer<float> trialInput (2, 256); trialInput.clear();
    require (controller.renderSelectedB (trialInput, gainedA + 48000, true, true, true), "arm active trial on audio callback");
    feedPassage (controller, fixture, gainedB, gainedA + 48000, 1.0f,
                 "blind-live-edit", true);
    require (!controller.answerBlind (1), "changed A cannot produce a valid Blind answer");
    controller.observeTransport (0, false, false); controller.endBlind();
    for (int i = 0; i < 500 && controller.snapshot().blindPhase != ref::BlindPhase::inactive; ++i) juce::Thread::sleep (10);
    require (controller.snapshot().blindPhase == ref::BlindPhase::inactive, "invalidated trial still has an END exit");
    require (controller.aInputFeeding(), "A goes back to the observation after an invalidated trial is ended");
    // VERSION BLIND が END を通らずに終わっても、A は観測へ戻り、ほかの Blind を締め出したままにしない。
    const auto startEndingTrial = [&] (const char* what)
    {
        // Blind が終わると V の位置合わせはやり直しになる。ほかの試験と同じく、始める前に A の区間を流す。
        feedPassage (controller, fixture, gainedB, gainedA + 48000, 0.5f, what);
        for (int i = 0; i < 300 && ! controller.snapshot().versionReady; ++i)
        { controller.observeTransport (gainedA + 48000, true, true); juce::Thread::sleep (10); }
        controller.observeTransport (gainedA + 48000, true, true); juce::Thread::sleep (100);
        if (! controller.startBlind (-28, -12))
        {
            const auto state = controller.snapshot();
            std::cerr << what << ": ready " << state.versionReady << " phase " << static_cast<int> (state.blindPhase)
                      << " reason " << state.rejectionCode << '\n';
            require (false, what);
        }
    };
    // 聴く前に終わった Blind（最初の block が bypass・書き出しで鳴らせず、作業スレッドが取り消す）。
    startEndingTrial ("a Version Blind that ends before it is heard");
    require (! controller.aInputFeeding(), "A pauses while the Version Blind starts");
    juce::AudioBuffer<float> bypassed (2, 256); bypassed.clear();
    controller.renderSelectedB (bypassed, gainedA + 48000, true, false, true);
    for (int i = 0; i < 500 && (controller.snapshot().blindPhase != ref::BlindPhase::inactive || ! controller.aInputFeeding()); ++i)
    { controller.observeTransport (gainedA + 48000, true, true); controller.servicePendingAudition (-23.0, -6.0, true); juce::Thread::sleep (10); }
    require (controller.snapshot().blindPhase == ref::BlindPhase::inactive && controller.aInputFeeding(),
             "A goes back to the observation when a Version Blind ends before it is heard");
    require (controller.reserveLocalBlind(), "the Blind slot is free after an unheard Version Blind");
    controller.releaseLocalBlind (0);
    owned.reset();
    versionBlindReloadEnds (sandbox);
    versionBlindWaitsForReturn (sandbox);
    std::cout << "calibration regressions: gain " << corrected << " dB; relocated identical PCM 48000 samples, mapping error 0 samples\n";
}
