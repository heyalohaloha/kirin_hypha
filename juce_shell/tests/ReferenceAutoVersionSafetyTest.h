#pragma once

// 2026-10-06：V の AUTO は V の選択だけを替える（INV-S51）。V が鳴っている・押して待っているあいだは V を替えず、
// 鳴っているほかの役（C）を止めない。AUTO が選んだことは DAW の曲に保存され、開き直しても AUTO のまま（手で選べば
// AUTO は替えない）。
namespace
{
void verifyReferenceAutoVersionSafety (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("auto-version-safety");
    require (root.createDirectory(), "AUTO safety fixture directory");
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
    require (writeJson (root.getChildFile ("library/manifest.json"), independentLibraryManifest (root, preset, 1)),
             "AUTO safety publication");
    ref::ReferenceComparisonController controller (root);
    controller.configure ({ "auto-safety-post", {}, 42, true }, 48000, 2);
    controller.setPresented (true);
    juce::AudioBuffer<float> block (2, 256);
    const auto host = [&] (bool playing) {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) block.setSample (c, i, 0.125f);
        controller.observeTransport (0, true, playing);
        controller.observeAInput (block, 0, true, playing, true);
        controller.renderSelectedB (block, 0, true, true, true);
    };
    const auto wait = [&] (const auto& condition, const char* what) {
        for (int i = 0; i < 1500; ++i) { if (condition (controller.snapshot())) return; juce::Thread::sleep (10); }
        require (false, what);
    };
    host (false);
    wait ([] (const auto& s) { return s.checkArmable && ! s.versions.empty(); }, "AUTO safety library");
    const auto autoId = controller.snapshot().versions.front().id;
    require (controller.selectVersion (autoId, true) && controller.snapshot().versionAuto, "AUTO chooses V while nothing plays");

    // 保存と読み戻し：AUTO が選んだことは DAW の曲に残り、開き直しても AUTO のまま。
    {
        juce::XmlElement saved ("STATE");
        controller.savedSettings().write (saved);
        const auto read = ref::ReferenceComparisonSettings::read (saved);
        require (read.versionAuto && read.version.target() == autoId, "the AUTO choice is saved as AUTO");
        ref::ReferenceComparisonController reopened (root);
        reopened.restoreSettings (read);
        reopened.configure ({ "auto-safety-reopened", {}, 43, true }, 48000, 2);
        bool restored = false;
        for (int i = 0; i < 1500 && ! restored; ++i)
        {
            const auto s = reopened.snapshot();
            restored = s.versionAuto && s.selectedVersionId == autoId;
            if (! restored) juce::Thread::sleep (10);
        }
        require (restored, "a reopened Hypha keeps the Version as AUTO's choice");
        auto manual = read;
        manual.versionAuto = false;
        juce::XmlElement manualXml ("STATE");
        manual.write (manualXml);
        require (! ref::ReferenceComparisonSettings::read (manualXml).versionAuto, "a Version the user chose is saved as theirs");
    }

    // V が鳴っているあいだ、AUTO は V を替えない。
    wait ([] (const auto& s) { return s.versionArmable; }, "AUTO's Version can be prepared");
    int cursor = 0;
    for (int i = 0; i < 1500 && ! controller.snapshot().versionReady; ++i)
    { observeWholeSongFixture (controller, fixture, cursor); juce::Thread::sleep (10); }
    require (controller.snapshot().versionReady, "AUTO's Version has verified content correspondence");
    require (controller.requestAudition (1, -28, -12) && controller.snapshot().audibleComparisonSlot == 1, "V plays AUTO's Version");
    require (! controller.selectVersion (autoId, true) && controller.snapshot().audibleComparisonSlot == 1
                 && controller.snapshot().versionAuto,
             "AUTO never changes V while V plays");
    // V を押して DAW の再生を待っているあいだも替えない。
    controller.selectA();
    host (false);
    require (controller.requestAudition (1, -28, -12) && controller.pendingSlot() == 1, "V waits for the DAW");
    require (! controller.selectVersion (autoId, true) && controller.pendingSlot() == 1, "AUTO never changes V while V waits");
    controller.selectA();
    // C が鳴っているあいだ、AUTO は V の選択だけを替え、C を止めない。
    for (int i = 0; i < 12; ++i) host (true);
    wait ([] (const auto& s) { return s.checkArmable; }, "C can play");
    require (controller.requestAudition (2, -28, -12) && controller.snapshot().audibleComparisonSlot == 2, "C plays");
    require (controller.selectVersion (autoId, true) && controller.snapshot().audibleComparisonSlot == 2
                 && controller.snapshot().bSelected,
             "AUTO changes only V's choice and C keeps playing");
    controller.selectA();
    // 手で選んだ Version は AUTO が替えない。
    require (controller.selectVersion (autoId) && ! controller.snapshot().versionAuto && ! controller.selectVersion (autoId, true),
             "AUTO never replaces a Version the user chose");
    std::cout << "Reference V AUTO: plays, waits, other roles, saved choice PASS\n";
}
}
