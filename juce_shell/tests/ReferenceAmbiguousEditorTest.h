#pragma once
#include "../src/reference_audition/ReferenceSourceSnapshot.h"
#include "../src/reference_audition/ReferencePinnedSource.h"
#include "../src/reference_audition/ReferenceVerifiedReader.h"

namespace hypha::tests::editor_product
{
inline juce::File editorOpenSource, editorOpenSaved;
inline std::atomic<bool> editorOpenFailed { false };
inline void failEditorSourceOpen (const ref::RuntimeSource& source)
{
    if (source.absolutePath != editorOpenSource.getFullPathName() || editorOpenFailed.exchange (true)) return;
    require (editorOpenSource.moveFileTo (editorOpenSaved), "make only the real fixture open unavailable after verification");
}
// B keeps its registered file so another role cannot consume C's injected open failure.
inline juce::File writeInitialCheckSource (const juce::File& root, const OneSongLibrary& song)
{
    const auto file = root.getChildFile ("check.wav");
    require (root.getChildFile (song.fileName).copyFileTo (file), "C owns a distinct source file");
    const auto hash = juce::SHA256 (file).toHexString(), pcm = juce::String::repeatedString ("7", 64);
    auto source = makeRuntimeV2Source (file, hash, pcm);
    source["audio"].getDynamicObject()->setProperty ("total_sample_frames", static_cast<juce::int64> (480000));
    addRuntimeV2MeasurementSummary (source, song.integratedLufs, song.truePeakDbtp);
    auto preset = bindRuntimeV2PresetToSource ("88888888-8888-4888-8888-888888888883", "99999999-9999-4999-8999-999999999993",
        stageRuntimeV2Artifact (root, "sources", source), hash, pcm);
    auto* object = preset.getDynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_preset"); object->setProperty ("version", "1.0");
    object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    require (writeJson (root.getChildFile ("library/manifest.json"), libraryManifest (root, preset, 1)), "publish C's distinct source");
    return file;
}
inline void verifyInitialSourceRecoveryFromEditor (const juce::File& sandbox)
{
    for (int failure = 0; failure < 5; ++failure)
    for (const bool stopped : { false, true })
    for (const bool cancel : { false, true })
    {
        if (failure < 2 && !stopped) continue;
        std::cerr << "initial source editor case=" << failure << " stopped=" << stopped << " cancel=" << cancel << std::endl;
        const bool storageFailure = failure > 0;
        const auto root = sandbox.getChildFile ("initial-editor-" + juce::String (failure)
            + juce::String (stopped ? 1 : 0) + juce::String (cancel ? 1 : 0));
        OneSongLibrary song;
        song.frequencyHz = 1500.0; song.integratedLufs = -26.7; song.truePeakDbtp = -26.0;
        song.fileName = "source.wav";
        writeOneSongLibrary (root, song);
        const auto file = writeInitialCheckSource (root, song), saved = root.getChildFile ("saved.wav");
        ref::revision_detail::SnapshotStorage reservation;
        if (storageFailure)
            require (reservation.reserve (file, root.getBytesFreeOnVolume() - ref::revision_detail::SnapshotStorage::headroom - 1024 * 1024),
                     "reserve temporary-copy capacity without allocating disk bytes");
        else require (file.moveFileTo (saved), "remove the real source before its first verification");
        struct Reset { ~Reset() {
            ref::revision_detail::sourceReaderOpenHookForTest.store (nullptr);
            ref::revision_detail::sourcePageOpenHookForTest.store (nullptr);
            ref::revision_detail::sourceInitialFormatOpenHookForTest.store (nullptr);
        } } reset;
        if (failure >= 2)
        {
            editorOpenSource = file; editorOpenSaved = saved; editorOpenFailed.store (false);
            if (failure == 2) ref::revision_detail::sourceReaderOpenHookForTest.store (&failEditorSourceOpen);
            else if (failure == 3) ref::revision_detail::sourcePageOpenHookForTest.store (&failEditorSourceOpen);
            else ref::revision_detail::sourceInitialFormatOpenHookForTest.store (&failEditorSourceOpen);
        }
        ref::RuntimeV2Repository::setTransportRootForTesting (root);
        ScopedChainTimingStorage preferences (root);
        NoticeRig r;
        r.clock.playing = !stopped; r.expectOrdinaryAudio = false;
        r.post->refreshLicenseForUserAction();
        r.view().onDomainChange (observatory::Domain::reference);
        auto due = std::chrono::steady_clock::now();
        const auto feed = [&] {
            r.block(); due += std::chrono::nanoseconds (512LL * 1'000'000'000LL / 48000);
            if (std::chrono::steady_clock::now() < due) std::this_thread::sleep_until (due);
        };
        // A slow host takes longer to verify and retry a source; the boundary is a state, not a time.
        const auto wait = [&] (auto predicate) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (60);
            while (!predicate() && std::chrono::steady_clock::now() < deadline) feed();
            return predicate();
        };
        require (wait ([&] {
            const auto state = r.post->referenceAuditionSnapshot();
            if (failure >= 2 && !editorOpenFailed.load()) return false;
            return failure == 1 ? state.checkReady : state.checkSelection
                && state.checkSelection->rejectionCode == (failure == 0 ? "reference_source_read_failed" : "reference_source_revalidating");
        }) || [&] {
            // Say where it stopped: the hook, the C selection and its code.
            const auto state = r.post->referenceAuditionSnapshot();
            std::cerr << "initial source wait ended: hook=" << editorOpenFailed.load() << " checkReady=" << state.checkReady
                      << " selection=" << (state.checkSelection ? 1 : 0) << " code="
                      << (state.checkSelection ? state.checkSelection->rejectionCode : juce::String ("-")) << std::endl;
            return false; }(), "the shipping processor reaches the actual initial source open boundary");
        (r.editor.get()->*privateMember (NoticeEditorTick {}))();
        const auto* panel = component<reference_ui::Component> (*r.editor);
        require (panel && panel->state().transportPlaying == !stopped && panel->state().checkArmable == (failure == 1),
                 "the real editor exposes the source state before the press");
        auto* button = dynamic_cast<juce::Button*> (find (*r.editor, "reference-c"));
        require (button != nullptr, "the shipping editor exposes the real C button");
        button->onClick(); feed();
        const auto queued = r.post->referenceAuditionSnapshot();
        require ((failure == 1 && !stopped && queued.bSelected)
                     || queued.switchingSlot == 2 || (queued.pendingAudition.waiting() && queued.pendingAudition.slot == 2),
                 "the stopped source button reaches the controller and retains its press during the read failure");
        if (cancel)
        {
            if (failure == 0 && stopped) r.view().onDomainChange (observatory::Domain::level);
            else
            {
                auto* a = dynamic_cast<juce::Button*> (find (*r.editor, "reference-a"));
                require (a != nullptr, "the shipping editor exposes A cancellation"); a->onClick();
            }
        }
        ref::revision_detail::sourceReaderOpenHookForTest.store (nullptr);
        ref::revision_detail::sourcePageOpenHookForTest.store (nullptr);
        ref::revision_detail::sourceInitialFormatOpenHookForTest.store (nullptr);
        if (failure != 1) require (saved.moveFileTo (file), "restore the same real source object");
        require (wait ([&] { return r.post->referenceAuditionSnapshot().checkReady; }), "the original selection recovers");
        if (stopped) require (r.post->referenceOutputSlot() == 0, "recovery while stopped starts no audio");
        r.clock.playing = true;
        if (!cancel) require (wait ([&] { return r.post->referenceOutputSlot() == 2; }),
                              "playback completes the held request without another press");
        for (int n = 0; n < 8; ++n) feed();
        const auto completed = r.post->referenceAuditionSnapshot();
        require (!cancel || (!completed.pendingAudition.waiting() && completed.switchingSlot == 0),
                 "A cancels every pending start after source recovery and live callbacks");
        double power = 0.0, sine = 0.0, cosine = 0.0;
        for (int n = 0; n < 512; ++n)
        {
            const auto out = r.postAudio.getSample (0, n);
            const auto phase = 6.283185307179586 * 1500.0 * n / 48000.0;
            power += out * out; sine += out * std::sin (phase); cosine += out * std::cos (phase);
        }
        const bool referenceHeard = power > 0 && 2 * (sine * sine + cosine * cosine) / (512 * power) > 0.9;
        require (referenceHeard == !cancel && r.post->referenceOutputSlot() == (cancel ? 0 : 2),
                 "the real button's held press reaches actual source PCM; pressing A cancels it");
        if (cancel && failure == 0 && stopped)
        {
            r.view().onDomainChange (observatory::Domain::reference);
            for (int n = 0; n < 8; ++n) feed();
            require (r.post->referenceOutputSlot() == 0 && !r.post->referenceAuditionSnapshot().pendingAudition.waiting(),
                     "leaving REF during a stopped preparation stays cancelled after recovery, playback and return");
        }
        std::cerr << "initial source editor case completed" << std::endl;
    }
}

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
