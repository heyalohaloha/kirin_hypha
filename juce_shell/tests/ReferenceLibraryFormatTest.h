#pragma once
#include "reference_library_manifest_fixture.h"
#include "ReferenceControllerTestAccess.h"
namespace
{
void verifyLibraryFormat (const juce::File& root, bool warm)
{
    auto preset = makeRuntimeV2Preset ("88888888-8888-4888-8888-888888888888", "99999999-9999-4999-8999-999999999999");
    if (warm)
    {
        require (root.createDirectory().wasOk(), "warm library fixture directory");
        const auto file = root.getChildFile ("version.wav");
        const juce::String recording = "22222222-2222-4222-8222-222222222222", version = "33333333-3333-4333-8333-333333333333";
        const auto fixture = makeWholeSongFixture (root, file, recording, version);
        preset = bindRuntimeV2WorkVersionPresetToSource ("88888888-8888-4888-8888-888888888888", "99999999-9999-4999-8999-999999999999",
            fixture.receipt, juce::SHA256 (file).toHexString(), fixture.pcmHash, recording, version, fixture.audio.getNumSamples());
        preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    }
    auto* object = preset.getDynamicObject(); object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0"); object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    const auto valid = libraryManifest (root, preset, 1);
    auto future = valid.clone(); future.getDynamicObject()->setProperty ("version", "99.0");
    const auto file = root.getChildFile ("library/manifest.json");
    require (writeJson (file, warm ? valid : future), "publish isolated library manifest");
    ref::RuntimeV2Controller controller (root, [] (bool) { return true; });
    controller.configure ({"library-format", {}, 42, true}, 48000, 2);
    const auto watchdog = [] { return std::chrono::steady_clock::now() + std::chrono::minutes (5); };
    namespace control = hypha::tests::reference_control;
    if (warm)
    {
        require (control::waitUntil (watchdog(), [&] {
            const auto state = controller.snapshot(); return state.libraryReceived && ! state.presets.empty();
        }), "the prior valid library was actually received");
        require (control::waitUntil (watchdog(), [&] {
            controller.observeTransport (0, true, true);
            const auto state = controller.snapshot(); return state.state == ref::RuntimeState::ready && state.auditionBuffered;
        }), "the prior library has real playable audio");
        require (controller.selectB (-22, -6) && controller.snapshot().bSelected, "explicitly select the prior playable reference");
        require (writeJson (file, future), "replace the prior library with a future format");
    }
    const bool unreadable = control::waitUntil (watchdog(), [&] {
        const auto state = controller.snapshot();
        return state.state == ref::RuntimeState::rejected && state.rejectionCode == "reference_library_rejected";
    });
    if (! unreadable)
    {
        const auto actual = controller.snapshot();
        std::cerr << "library format missing warm=" << warm << " state=" << static_cast<int> (actual.state)
                  << " code=" << actual.rejectionCode << " bSelected=" << actual.bSelected << '\n';
    }
    require (unreadable, "unreadable cold and retained libraries publish their rejection reason");
    const auto rejected = controller.snapshot();
    require (! rejected.bSelected && ! rejected.auditionBuffered, "unreadable library cannot acquire the output");
    require (rejected.libraryReceived == warm && (! warm || ! rejected.presets.empty()), "retained catalog remains presentation data only");
    require (! rejected.alignmentPrepared && ! rejected.cueLevelAvailable && ! rejected.aCaptureAvailable
        && ! rejected.measurementAvailable && ! rejected.detailedMeasurement && ! rejected.versionReady,
        "retained catalog carries no previous measurement or readiness");
    if (warm)
    {
        controller.selectPreset (rejected.presetId); controller.selectCandidate (rejected.candidateId);
        require (! controller.selectB (-22, -6) && ! controller.snapshot().bSelected,
                 "clicking a retained catalog item cannot play rejected audio");
        juce::AudioBuffer<float> audio (2, 128); audio.clear();
        require (! controller.renderSelectedB (audio, 0, true, true, true), "retained catalog keeps A on the real output path");
    }
    require (writeJson (file, valid), "restore a valid fixture publication");
    require (control::waitUntil (watchdog(), [&] {
        const auto state = controller.snapshot(); return state.libraryReceived && state.rejectionCode != "reference_library_rejected";
    }), "a later valid library clears the format rejection");
    require (! controller.snapshot().bSelected, "recovery never restores an old audition permission");
}
}
void testReferenceLibraryFormat (const juce::File& sandbox)
{
    for (bool warm : {false, true}) verifyLibraryFormat (sandbox.getNonexistentChildFile ("library-format", {}, false), warm);
    std::cout << "library format runtime: cold/warm/recovery PASS\n";
}
