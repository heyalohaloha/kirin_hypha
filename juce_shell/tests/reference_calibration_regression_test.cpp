#include "reference_runtime_v2_analysis_test_support.h"
#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"

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
    controller.observeTransport (0, false, false); controller.endBlind(); juce::Thread::sleep (100);
    controller.observeAInput (input, 0, false, false, true);
    bool drained = false;
    for (int i = 0; i < 500; ++i)
    {
        controller.observeTransport (0, false, false); juce::Thread::sleep (10);
        const auto state = controller.snapshot();
        if (!state.aCaptureAvailable && state.blindPhase == ref::BlindPhase::inactive) { drained = true; break; }
    }
    require (drained, "paused capture discontinuity is consumed before the next exact four-second observation");
    return trial;
}

void feedPassage (ref::ReferenceComparisonController& controller, const WholeSongFixture& fixture,
                  int sourceStart, int hostStart, float gain, bool duringBlind = false)
{
    for (int frame = 0; frame < 192000; frame += 256)
    {
        juce::AudioBuffer<float> input (2, 256);
        for (int c = 0; c < 2; ++c) input.copyFrom (c, 0, fixture.audio, c, sourceStart + frame, 256);
        input.applyGain (gain);
        controller.observeTransport (hostStart + frame, true, true);
        controller.observeAInput (input, hostStart + frame, true, true, true);
        juce::Thread::sleep (6);
    }
    for (int i = 0; i < 500; ++i)
    {
        controller.observeTransport (hostStart + 191744, true, true); juce::Thread::sleep (10);
        const auto state = controller.snapshot();
        if (state.aCaptureAvailable && (duringBlind ? state.blindPhase == ref::BlindPhase::invalidated : state.versionReady)) return;
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
}

void testReferenceCalibrationRegressions (const juce::File& sandbox);
void testReferenceCalibrationRegressions (const juce::File& sandbox)
{
    observationBoundaries();
    const auto root = sandbox.getChildFile ("calibration-regressions");
    require (root.createDirectory().wasOk(), "calibration fixture directory");
    const auto file = root.getChildFile ("version.wav");
    const juce::String recording = "22222222-2222-4222-8222-222222222222", version = "33333333-3333-4333-8333-333333333333";
    const auto fixture = makeWholeSongFixture (root, file, recording, version);
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
    ref::ReferenceComparisonController controller (root);
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
    require (controller.savedSettings().version.target() == controller.snapshot().versions[0].id,
             "legacy C-based B choice migrates through its immutable receipt after C removal");
    const auto initial = calibrationReceipt (controller, fixture, root);
    const int a = static_cast<int> (initial["calibration"]["a"]["start_sample"]);
    const int b = static_cast<int> (initial["calibration"]["b"]["start_sample"]);
    feedPassage (controller, fixture, b, a, 0.5f);
    require (controller.selectB (-28.0206, -12.0206), "B is available after repeated-passage gain edit");
    const auto corrected = controller.snapshot().appliedGainDb;
    require (std::abs (corrected + 6.0205999) < 0.01, "B gain follows the revised A calibration, not the old 0 dB receipt");
    controller.selectA();
    const auto gained = calibrationReceipt (controller, fixture, root);
    const auto hash = gained["sources"]["a"]["observation_pcm_sha256"].toString();
    const int gainedA = static_cast<int> (gained["calibration"]["a"]["start_sample"]);
    const int gainedB = static_cast<int> (gained["calibration"]["b"]["start_sample"]);
    feedPassage (controller, fixture, gainedB, gainedA + 48000, 0.5f);
    const auto moved = calibrationReceipt (controller, fixture, root, gainedA + 48000);
    require (moved["sources"]["a"]["observation_pcm_sha256"] == hash, "relocation reuses exactly the same PCM hash");
    require (static_cast<int> (moved["calibration"]["a"]["start_sample"]) == gainedA + 48000
        && static_cast<int> (moved["calibration"]["b"]["start_sample"]) == gainedB,
        "identical PCM moved by one second receives a new host/source map");
    controller.observeTransport (gainedA + 48000, true, true); juce::Thread::sleep (100);
    require (controller.startBlind (-28, -12), "start trial before another live A edit");
    juce::AudioBuffer<float> trialInput (2, 256); trialInput.clear();
    require (controller.renderSelectedB (trialInput, gainedA + 48000, true, true, true), "arm active trial on audio callback");
    feedPassage (controller, fixture, gainedB, gainedA + 48000, 1.0f, true);
    require (!controller.answerBlind (1), "changed A cannot produce a valid Blind answer");
    controller.observeTransport (0, false, false); controller.endBlind();
    for (int i = 0; i < 500 && controller.snapshot().blindPhase != ref::BlindPhase::inactive; ++i) juce::Thread::sleep (10);
    require (controller.snapshot().blindPhase == ref::BlindPhase::inactive, "invalidated trial still has an END exit");
    std::cout << "calibration regressions: gain " << corrected << " dB; relocated identical PCM 48000 samples, mapping error 0 samples\n";
}
