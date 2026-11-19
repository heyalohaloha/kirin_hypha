#pragma once
#include "EditorProductChecks.h"
#include "ReferenceSelectionReopenTest.h"
#include <chrono>
#include "ReferenceOneSongLibrary.h"
#include "../src/PluginEditorLu.h"

namespace hypha::tests::editor_product
{
struct LuEditorTick { using Type = void (KirinHyphaEditor::*)(); friend Type privateMember (LuEditorTick); };
template struct PrivateAccess<LuEditorTick, &KirinHyphaEditor::timerCallback>;
struct LuEnableWrites { using Type = void (Processor::*)(); friend Type privateMember (LuEnableWrites); };
template struct PrivateAccess<LuEnableWrites, &Processor::enableWritesNow>;
struct LuProcessorTick { using Type = void (Processor::*)(); friend Type privateMember (LuProcessorTick); };
template struct PrivateAccess<LuProcessorTick, &Processor::timerCallback>;
// Ordinary real-clock processor fixture: no LISTEN and no test-clock substitute.
struct LuRig : private juce::Timer {
    std::unique_ptr<Processor> pre, post;
    juce::AudioBuffer<float> audio { 2, 512 }; juce::MidiBuffer midi;
    LuRig() {
        for (auto role : { Processor::Role::Pre, Processor::Role::Post }) {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            auto p = std::make_unique<Processor> (role);
            auto layout = p->getBusesLayout();
            layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
            layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
            require (p->setBusesLayout (layout), "LU fixture stereo");
            prepareFixtureProcessor (*p, 48000, 512);
            (p.get()->*privateMember (LuEnableWrites {}))();
            (role == Processor::Role::Pre ? pre : post) = std::move (p);
        }
    }
    ~LuRig() { stopTimer(); pre->releaseResources(); post->releaseResources(); }
    std::uint64_t renderedFrames = 0;
    void timerCallback() override {
        audio.clear(); pre->processBlock (audio, midi);
        audio.clear(); post->processBlock (audio, midi);
        (pre.get()->*privateMember (LuProcessorTick {}))();
        (post.get()->*privateMember (LuProcessorTick {}))();
        renderedFrames += static_cast<std::uint64_t> (audio.getNumSamples());
    }
    void advance (double seconds) {
        const auto target = renderedFrames + static_cast<std::uint64_t> (std::ceil (seconds * 48000.0));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (10);
        startTimer (11); // Deliver real audio-block and UI notifications, not a sleep budget.
        while (renderedFrames < target && std::chrono::steady_clock::now() < deadline) {
           #if JUCE_MAC
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.05, true);
           #else
            juce::detail::dispatchNextMessageOnSystemQueue (true);
           #endif
        }
        stopTimer();
        require (renderedFrames >= target, "LU audio-block notifications reach requested sample count");
    }
    void selectPair() {
        auto preview = post->createPairPreview();
        require (pair_preview::request (preview), "LU real PRE discovery request");
        for (int n=0; n<100; ++n) {
            advance (0.1); KirinPairPreviewValue value {};
            if (kirin_hypha_pair_preview_poll (preview.get(), &value) && value.complete && value.has_single) {
                require (post->setPairCandidate (pre->instanceId(), "LU fixture PRE"), "LU real PRE selected"); return;
            }
            if (n%10==9) pair_preview::request (preview);
        }
        require (false, "LU PRE discovery deadline");
    }
};
// Real processor publication and shipping PRE editor. No POST editor exists
// during restoration; no shared file is injected by this test.
inline void verifyLuProcessorEditor (const juce::File& sandbox)
{
    namespace ref = reference_audition;
    struct RestoreTransportRoot {
        juce::File previous = ref::RuntimeV2Repository::transportRoot();
        ~RestoreTransportRoot() { ref::RuntimeV2Repository::setTransportRootForTesting (previous); }
    } restoreTransportRoot;
    const auto root = sandbox.getNonexistentChildFile ("lu-reference", {}, false);
    require (root.isAChildOf (sandbox) && root.createDirectory().wasOk(), "isolated LU Reference fixture");
    ref::RuntimeV2Repository::setTransportRootForTesting (root);
   #if JUCE_WINDOWS
    const auto identity = sandbox.getChildFile ("home/AppData/Roaming/Kirin OS/identity.json");
   #else
    const auto identity = sandbox.getChildFile ("home/Library/Application Support/Kirin OS/identity.json");
   #endif
    require (root.isAChildOf (sandbox) && identity.isAChildOf (sandbox)
        && ui_preferences::defaultFile().isAChildOf (sandbox), "LU uses fixture storage only");
    require (identity.getParentDirectory().createDirectory().wasOk()
        && storage::writeText (identity, "{\"license\":\"os\"}"), "LU fixture access");
    LuRig rig;
    rig.pre->refreshLicenseForUserAction(); rig.post->refreshLicenseForUserAction();
    require (rig.pre->licenseIsOs() && rig.post->licenseIsOs(), "LU fixture entitlement");
    rig.selectPair();
    std::unique_ptr<KirinHyphaEditor> preEditor (dynamic_cast<KirinHyphaEditor*> (rig.pre->createEditorIfNeeded()));
    require (preEditor != nullptr, "actual PRE editor"); preEditor->setVisible (true);
    auto* view = component<observatory::View> (*preEditor);
    require (view != nullptr, "actual PRE view");
    const auto refresh = [&] { (preEditor.get()->*privateMember (LuEditorTick {}))(); };
    const auto wait = [&] (auto condition, const char* message) {
        for (int n = 0; n < 80 && !condition(); ++n) { rig.advance (0.1); refresh(); }
        if (!condition()) {
            const auto runtime = rig.post->referenceAuditionSnapshot();
            const auto f = view->lu().facts();
            std::cerr << "LU diagnostic: " << message << " library=" << runtime.libraryReceived
                << " preScope=" << rig.pre->hybridVuCalibrationScope() << " postScope=" << rig.post->hybridVuCalibrationScope()
                << " readable=" << f.readable << " zero=" << static_cast<int>(f.zero) << " song=" << f.song << '\n';
        }
        require (condition(), message);
    };
    rig.advance (0.3); refresh();
    require (!view->lu().facts().readable, "no received library never offers LU in PRE");
    // OneSongLibrary has summary-only data; LU's existing contract requires a
    // bound detailed measurement or Cue range. Supply a real accepted artifact.
    const auto withDetails = [&] (juce::var& entry) {
        const auto sourceHash = entry["source_artifact"]["sha256"].toString();
        auto source = juce::JSON::parse (root.getChildFile ("sources/" + sourceHash + ".json"));
        auto measurement = makeRuntimeV2Measurement (entry["source_identity"]["sha256_file"].toString(),
                                                    entry["source_identity"]["sha256_pcm"].toString());
        measurement["audio"].getDynamicObject()->setProperty ("total_sample_frames", static_cast<juce::int64>(480000));
        measurement["views"]["waveform"].getDynamicObject()->setProperty ("frames_per_bin", static_cast<juce::int64>(120000));
        measurement["views"]["loudness"].getDynamicObject()->setProperty ("hop_samples", static_cast<juce::int64>(120000));
        const auto object = [] (const ref::RuntimeContentReceipt& receipt) {
            auto* value = new juce::DynamicObject();
            value->setProperty ("relative_path", receipt.relativePath); value->setProperty ("sha256", receipt.sha256);
            value->setProperty ("bytes", receipt.bytes); return juce::var (value);
        };
        source["measurement"].getDynamicObject()->setProperty ("detail_artifact", object (stageRuntimeV2Artifact (root, "measurements", measurement)));
        entry.getDynamicObject()->setProperty ("source_artifact", object (stageRuntimeV2Artifact (root, "sources", source)));
    };
    OneSongLibrary a; a.displayName = "LU song A"; a.fileName = "lu-a.wav"; a.integratedLufs = -24.0;
    writeOneSongLibrary (root, a);
    auto aSets = juce::JSON::parse (root.getChildFile ("library/sets.json"));
    auto aEntry = aSets["song_sets"][0]["songs"][0]; withDetails (aEntry);
    OneSongLibrary b; b.displayName = "LU song B"; b.fileName = "lu-b.wav"; b.integratedLufs = -12.0; b.frequencyHz = 700.0;
    writeOneSongLibrary (root, b, 2);
    auto sets = juce::JSON::parse (root.getChildFile ("library/sets.json"));
    auto bEntry = sets["song_sets"][0]["songs"][0]; withDetails (bEntry);
    bEntry.getDynamicObject()->setProperty ("candidate_id", "59595959-5959-4959-8959-595959595958");
    sets["song_sets"][0].getDynamicObject()->setProperty ("songs", juce::Array<juce::var> { aEntry, bEntry });
    require (writeJson (root.getChildFile ("library/sets.json"), sets), "publish two distinct LU fixtures");
    wait ([&] {
        const auto snapshot = rig.post->referenceAuditionSnapshot();
        bool hasA = false, hasB = false;
        for (const auto& set : snapshot.songSets) for (const auto& song : set.songs) {
            hasA = hasA || song.id.endsWith ("/59595959-5959-4959-8959-595959595959");
            hasB = hasB || song.id.endsWith ("/59595959-5959-4959-8959-595959595958");
        }
        return snapshot.libraryReceived && hasA && hasB;
    }, "processor receives both published songs without editor");
    juce::MemoryBlock original; rig.post->getStateInformation (original);
    auto state = juce::AudioProcessor::getXmlFromBinary (original.getData(), static_cast<int> (original.getSize()));
    require (state != nullptr, "real saved processor state");
    const auto restore = [&] (const juce::String& id) {
        auto xml = std::make_unique<juce::XmlElement> (*state);
        xml->deleteAllChildElementsWithTagName ("ReferenceChoices");
        ref::ReferenceComparisonSettings settings;
        settings.viewedSlot = 3; settings.songSetId = "69696969-6969-4969-8969-696969696969";
        if (id.isNotEmpty())
        {
            juce::String selected;
            for (const auto& set : rig.post->referenceAuditionSnapshot().songSets)
                for (const auto& song : set.songs)
                    if (song.id.endsWith ("/" + id)) selected = song.id;
            const auto parts = juce::StringArray::fromTokens (selected, "/", {});
            require (parts.size() == 3, "restore uses the published song identity");
            settings.reference = { parts[0], parts[1], parts[2], {} };
        }
        settings.write (*xml); juce::MemoryBlock saved;
        juce::AudioProcessor::copyXmlToBinary (*xml, saved);
        rig.post->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    };
    restore ("59595959-5959-4959-8959-595959595959");
    wait ([&] { const auto& f = view->lu().facts(); return f.song == a.displayName && std::abs (f.zeroLufs + 24.0) < 0.01; }, "closed POST restore A publishes A zero");
    restore ("59595959-5959-4959-8959-595959595958");
    wait ([&] { const auto& f = view->lu().facts(); return f.song == b.displayName && std::abs (f.zeroLufs + 12.0) < 0.01; }, "closed POST restore B replaces A zero and name");
    // The product auto-selects the first available song. Supervisor clarified
    // that unselected means a received library with zero songs, not a new flag.
    auto manifest = juce::JSON::parse (root.getChildFile ("library/manifest.json"));
    auto emptyPreset = juce::JSON::parse (root.getChildFile ("library/presets/"
        + manifest["presets"][0]["sha256"].toString() + ".json"));
    emptyPreset.getDynamicObject()->setProperty ("checks", juce::Array<juce::var>());
    auto emptyManifest = libraryManifest (root, emptyPreset, 3);
    auto emptySets = sets.clone();
    emptySets.getDynamicObject()->setProperty ("revision", 3);
    emptySets.getDynamicObject()->setProperty ("manifest_revision", 3);
    emptySets.getDynamicObject()->setProperty ("song_sets", juce::Array<juce::var>());
    require (writeJson (root.getChildFile ("library/sets.json"), emptySets)
        && writeJson (root.getChildFile ("library/manifest.json"), emptyManifest), "received empty library fixture");
    restore ({});
    wait ([&] { const auto& f = view->lu().facts(); return f.readable && f.zero == lu_needle::Zero::noSong && f.song.isEmpty(); }, "deselect removes old zero and name");
    rig.pre->editorBeingDeleted (preEditor.get()); preEditor.reset();
    std::cout << "LU processor/editor unreadable/A/B/empty-library: PASS\n";
}
}
