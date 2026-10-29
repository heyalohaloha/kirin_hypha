#pragma once

namespace hypha::tests::editor_product
{
inline void verifyAmbiguousVersionFromEditor (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("ambiguous-editor");
    require (root.createDirectory().wasOk(), "ambiguous editor fixture directory");
    const auto file = root.getChildFile ("source.wav");
    const auto fixture = makeWholeSongFixture (root, file,
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333", 2, false, true, 18);
    auto preset = bindRuntimeV2WorkVersionPresetToSource (
        "88888888-8888-4888-8888-888888888888", "99999999-9999-4999-8999-999999999999",
        fixture.receipt, juce::SHA256 (file).toHexString(), fixture.pcmHash,
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333", fixture.audio.getNumSamples());
    auto* object = preset.getDynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0");
    object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    require (writeJson (root.getChildFile ("library/manifest.json"), independentLibraryManifest (root, preset, 1)),
             "ambiguous editor library publication");
    ref::RuntimeV2Repository::setTransportRootForTesting (root);
    ScopedChainTimingStorage preferences (sandbox);
    NoticeRig r;
    r.expectOrdinaryAudio = false;
    r.post->refreshLicenseForUserAction();
    require (r.post->licenseIsOs(), "ambiguous editor has the sandbox license");
    r.view().onDomainChange (observatory::Domain::reference);
    auto due = std::chrono::steady_clock::now();
    auto feed = [&] {
        const auto position = static_cast<int> (r.clock.position);
        require (position + 512 <= fixture.audio.getNumSamples(), "fixture playback stays inside the song");
        for (int c = 0; c < 2; ++c)
        {
            r.preAudio.copyFrom (c, 0, fixture.audio, c, position, 512);
            r.postAudio.copyFrom (c, 0, fixture.audio, c, position, 512);
        }
        r.pre->processBlock (r.preAudio, r.midi);
        r.post->processBlock (r.postAudio, r.midi);
        r.wallClock.advanceBlock (512, 48000); r.clock.position += 512;
        (r.post.get()->*privateMember (NoticeProcessorTick {}))();
        (r.editor.get()->*privateMember (NoticeEditorTick {}))();
        due += std::chrono::nanoseconds (512LL * 1000000000LL / 48000);
        if (std::chrono::steady_clock::now() < due) std::this_thread::sleep_until (due);
    };
    for (int n = 0; n < 200 && r.post->referenceAuditionSnapshot().versions.empty(); ++n) feed();
    auto snapshot = r.post->referenceAuditionSnapshot();
    require (! snapshot.versions.empty() && r.post->selectReferenceVersion (snapshot.versions.front().id),
             "select the repeated Version in the shipping processor");
    r.clock.position = 0;
    for (int n = 0; n < 740; ++n)
    {
        snapshot = r.post->referenceAuditionSnapshot();
        if (snapshot.versionSelection && snapshot.versionSelection->rejectionCode == "reference_alignment_ambiguous") break;
        feed();
    }
    snapshot = r.post->referenceAuditionSnapshot();
    if (! snapshot.versionSelection || snapshot.versionSelection->rejectionCode != "reference_alignment_ambiguous")
        std::cerr << "ambiguous editor cursor=" << r.clock.position << " version="
                  << (snapshot.versionSelection ? snapshot.versionSelection->rejectionCode : juce::String ("absent"))
                  << " state=" << static_cast<int> (snapshot.state) << '\n';
    require (snapshot.versionSelection && snapshot.versionSelection->rejectionCode == "reference_alignment_ambiguous",
             "the shipping editor reaches ambiguous alignment");
    (r.editor.get()->*privateMember (NoticeEditorTick {}))();
    auto* button = dynamic_cast<juce::Button*> (find (*r.editor, "reference-b"));
    require (button != nullptr, "the shipping editor has the V button");
    button->onClick();
    (r.post.get()->*privateMember (NoticeProcessorTick {}))();
    const auto* panel = component<reference_ui::Component> (*r.editor);
    if (! r.post->referenceAuditionSnapshot().pendingAudition.waiting())
    {
        const auto& state = panel->state();
        std::cerr << "V click: step=" << static_cast<int> (state.versionStep)
                  << " playing=" << state.transportPlaying << " separate=" << state.separateComparisons
                  << " library=" << state.libraryReceived << " large=" << state.blindLargeScreen
                  << " access=" << static_cast<int> (state.osAccess) << " feedback=" << r.view().feedback() << '\n';
    }
    require (r.post->referenceAuditionSnapshot().pendingAudition.waiting() && r.post->referenceOutputSlot() == 0,
             "pressing V during ambiguity queues it while A remains audible");
    r.clock.position = 48000 * 7;
    for (int n = 0; n < 900 && r.post->referenceOutputSlot() != 1; ++n) feed();
    if (r.post->referenceOutputSlot() != 1)
    {
        const auto last = r.post->referenceAuditionSnapshot();
        std::cerr << "V after distinct passage: cursor=" << r.clock.position
                  << " reason=" << (last.versionSelection ? last.versionSelection->rejectionCode : juce::String ("absent"))
                  << " ready=" << last.versionReady << " pending=" << static_cast<int> (last.pendingAudition.stage)
                  << " pending reason=" << last.pendingAudition.reasonCode << " audible=" << last.audibleComparisonSlot << '\n';
    }
    require (r.post->referenceOutputSlot() == 1, "V becomes audible automatically after a distinct passage aligns");
}
}
