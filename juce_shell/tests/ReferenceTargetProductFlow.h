#pragma once
#include "../src/HyphaReferenceComponent.h"
#include "../src/HyphaObservatoryContract.h"
#include "../src/HyphaReferenceCueSummary.h"
#include "../src/HyphaReferenceStatusModel.h"
#include "../src/PluginEditor.h"
#include <chrono>

namespace hypha::tests::target_editor
{
struct HoldTag {};
void holdTimeline (KirinHyphaEditor&, std::shared_ptr<const reference_audition::VisualTimeline>, HoldTag);
void refresh (KirinHyphaEditor&, HoldTag);
template <auto Timeline, auto Deadline, auto Refresh> struct HoldAccess
{
    friend void holdTimeline (KirinHyphaEditor& editor, std::shared_ptr<const reference_audition::VisualTimeline> timeline, HoldTag)
    {
        editor.*Timeline = std::move (timeline);
        editor.*Deadline = juce::Time::getMillisecondCounterHiRes() + 3000;
    }
    friend void refresh (KirinHyphaEditor& editor, HoldTag) { (editor.*Refresh) ({}, false); }
};
template struct HoldAccess<&KirinHyphaEditor::referenceSwitchTimeline, &KirinHyphaEditor::referenceSwitchHoldUntilMs,
                          &KirinHyphaEditor::refreshReferenceAudition>;
}

// Runs inside the existing product fixture's event loop and real audio callback.
class ReferenceTargetProductFlow
{
public:
    bool advance (KirinHyphaProcessorBase& processor, const juce::File& input, const juce::File& root,
                  std::atomic<bool>& playing)
    {
        using Action = hypha::reference_audition::TargetRequest::Action;
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds (30))
        {
            const auto runtime = processor.referenceAuditionSnapshot();
            std::cerr << "Target stage " << stage << " / license " << processor.licenseIsOs()
                << " / A frames " << (runtime.visualTimeline && runtime.visualTimeline->aKirin ? runtime.visualTimeline->aKirin->frames : 0)
                << " / Check " << runtime.checkReady << " / MATCH blocks " << processor.referenceWindowLoudness (2).neededBlocks
                << " / editor visible " << (editor && editor->isVisible()) << '\n';
            require (false, "target product makes progress");
        }
        if (! editor)
        {
            processor.setObservatoryDomainPreference (static_cast<uint8_t> (hypha::observatory::Domain::reference));
            editor.reset (processor.createEditorIfNeeded());
            require (editor != nullptr, "target product creates the shipping editor");
            editor->setVisible (true);
            editor->setSize (900, 600);
        }
        auto* panel = findPanel (*editor);
        if (! panel || ! panel->isVisible()) return false;
        const auto runtime = processor.referenceAuditionSnapshot();
        if (stage == 0)
        {
            require (! runtime.libraryReceived, "local target creation needs no Kirin OS publication");
            if (! windowChosen)
            {
                if (! processor.selectReferenceAWindow (runtime.aWindowTarget, hypha::reference_audition::AWindowChoice::seconds3)) return false;
                windowChosen = true;
            }
            if (! runtime.visualTimeline || ! runtime.visualTimeline->aKirin || runtime.visualTimeline->aKirin->frames < 30) return false;
            auto* bar = panel->findChildWithID ("reference-targets");
            require (bar && bar->isVisible(), "licensed owner can reach target controls before registration");
            auto* make = dynamic_cast<juce::Button*> (bar->findChildWithID ("reference-make-target"));
            if (! make || ! make->isEnabled()) return false;
            make->onClick();
            const auto targets = processor.referenceTargetSnapshot();
            require (targets.collection.items.size() == 1 && targets.collection.selected.isNotEmpty(),
                     "the real MAKE TARGET button stores and selects A's window");
            selected = targets.collection.selected;
            openedEpoch = targets.restoreEpoch;
            require (! processor.selectReferenceC(), "a numerical C cannot start a hidden audio source");
            processor.getStateInformation (saved);
            const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
            require (xml && xml->getChildByName ("MeasuredTargets")
                && ! xml->getChildByName ("MeasuredTargets")->toString().contains (input.getFullPathName()),
                "the product's project state saves measurements without audio or source paths");
            juce::StringArray files;
            for (int i = 0; i < 10; ++i) files.add (input.getFullPathName());
            panel->filesDropped (files, 0, 0);
            stage = 1; started = std::chrono::steady_clock::now();
            return false;
        }
        if (stage == 1)
        {
            if (! panel->state().targetSelected) return false;
            require (panel->state().aWindowBlocks == 30
                && std::isfinite (hypha::reference_ui::comparisonGainDb (panel->state())),
                "the shipping editor compares a numeric target from A's three-second observation");
            const auto levels = hypha::reference_audition::targetWindowLevels (*panel->state().visualTimeline);
            require (std::abs (panel->state().aIntegratedLoudness - levels.lufs) < 1.0e-9
                && std::abs (panel->state().aMaximumTruePeakDbtp - levels.maxTruePeak) < 1.0e-9,
                "the shipping editor's numeric metrics use its displayed A window");
            if (! compactRequested)
            {
                editor->setSize (300, 200); compactRequested = true; return false;
            }
            if (! expanded)
            {
                if (panel->state().blindLargeScreen) return false;
                auto* data = dynamic_cast<juce::Button*> (panel->findChildWithID ("reference-target-data"));
                require (data && data->isVisible(), "numeric C has a real compact DATA entrance");
                data->onClick();
                require (editor->getWidth() == 900 && editor->getHeight() == 600
                    && ! processor.referenceAuditionSnapshot().bSelected,
                    "the shipping DATA button expands to the comparison charts without starting playback");
                expanded = true;
            }
            const auto targets = processor.referenceTargetSnapshot();
            require (targets.collection.selected == selected, "file measurement preserves the displayed C target");
            if (targets.measuring) return false;
            require (targets.collection.items.size() == 8 && targets.issue == "8 TARGETS MAX",
                     "the real REF drop fills the seven remaining slots and reports excess files");
            processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            const auto round = processor.referenceTargetSnapshot();
            require (round.collection.items.size() == 1 && round.collection.selected == selected
                && ! processor.referenceAuditionSnapshot().bSelected, "processor state round trip retains facts and never starts playback");
            require (! processor.changeReferenceTarget ({ Action::select, {}, {}, openedEpoch })
                && processor.referenceTargetSnapshot().collection.selected == selected,
                "a stale menu reply cannot overwrite the processor's restored selection");
            require (processor.changeReferenceTarget ({ Action::importFiles, { input.getFullPathName() }, {} }), "product import starts again");
            require (processor.changeReferenceTarget ({ Action::cancel, {}, {} }), "product cancel action succeeds");
            require (! processor.referenceTargetSnapshot().measuring
                && processor.referenceTargetSnapshot().collection.items.size() == 1, "product cancellation keeps the prior target");
            juce::XmlElement old ("KirinHyphaState");
            juce::MemoryBlock oldData; juce::AudioProcessor::copyXmlToBinary (old, oldData);
            processor.setStateInformation (oldData.getData(), static_cast<int> (oldData.getSize()));
            require (processor.referenceTargetSnapshot().collection.items.empty(), "old project state clears only the new target collection");
            processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            require (processor.changeReferenceTarget ({ Action::importFiles, { input.getFullPathName() }, {} }), "import overlaps legacy state restore");
            const juce::String legacy (R"({"version":"1.1.26","params":{},"fields":{"name":"\"Legacy\""}})");
            processor.setStateInformation (legacy.toRawUTF8(), static_cast<int> (legacy.getNumBytesAsUTF8()));
            const auto cleared = processor.referenceTargetSnapshot();
            require (cleared.collection.items.empty() && cleared.collection.selected.isEmpty() && ! cleared.measuring,
                     "accepted legacy JSON clears local targets, selection and the active import");
            processor.setObservatoryDomainPreference (static_cast<uint8_t> (hypha::observatory::Domain::reference));
            const auto shortFile = root.getChildFile ("short-target.wav");
            require (root.createDirectory().wasOk(), "short target fixture directory");
            juce::WavAudioFormat format;
            juce::AudioBuffer<float> shortAudio (1, 96000);
            for (int n = 0; n < shortAudio.getNumSamples(); ++n)
                shortAudio.setSample (0, n, 0.05f * static_cast<float> (std::sin (n * 0.13)));
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (shortFile.createOutputStream().release(), 48000, 1, 24, {}, 0));
            require (writer && writer->writeFromAudioSampleBuffer (shortAudio, 0, shortAudio.getNumSamples()), "short target fixture");
            writer.reset();
            require (processor.changeReferenceTarget ({ Action::importFiles, { shortFile.getFullPathName() }, {} }), "a short import follows the cancelled legacy generation");
            stage = 2; started = std::chrono::steady_clock::now(); return false;
        }
        if (stage == 2)
        {
            const auto targets = processor.referenceTargetSnapshot();
            if (targets.measuring) return false;
            require (targets.collection.items.size() == 1 && targets.collection.selected.isEmpty(),
                     "after both worker jobs settle only the post-restore import is published");
            if (! processor.selectReferenceAWindow (runtime.aWindowTarget, hypha::reference_audition::AWindowChoice::cue)) return false;
            selected = targets.collection.items.front().id;
            require (processor.changeReferenceTarget ({ Action::select, {}, selected }), "select the new numeric target");
            writeLongCheck (root);
            stage = 3; started = std::chrono::steady_clock::now(); return false;
        }
        if (stage == 3)
        {
            const auto& check = runtime.checkSelection ? *runtime.checkSelection : runtime;
            if (! presetChosen)
            {
                if (check.presets.empty() || ! processor.selectReferencePreset (check.presets.front().id)) return false;
                presetChosen = true; return false;
            }
            const auto match = processor.referenceWindowLoudness (2);
            if (check.state != hypha::reference_audition::RuntimeState::ready || ! check.auditionBuffered || match.neededBlocks != 300) return false;
            if (processor.referenceTargetSnapshot().collection.selected != selected)
            { require (processor.changeReferenceTarget ({ Action::select, {}, selected }), "select numeric C after the long Check"); return false; }
            if (! panel->state().targetSelected) return false;
            if (! panel->state().visualTimeline || panel->state().visualTimeline->binding.aDisplayWindowBlocks != 30) return false;
            if (panel->state().aWindowBlocks < 30) return false;
            require (panel->state().aWindowBlocks == 30
                && std::isfinite (hypha::reference_ui::comparisonGainDb (panel->state())),
                "a two-second target uses at least three seconds of A in default CUE, even beside a thirty-second Check");
            const auto* selectedTarget = panel->state().targets.collection.find (selected);
            require (selectedTarget && selectedTarget->channels == 1
                && std::abs (panel->state().cueLoudnessOffsetLu - 10.0 * std::log10 (2.0)) < 1.0e-12
                && std::abs (panel->state().adjustedBIntegratedLoudness - selectedTarget->integrated
                             - panel->state().cueLoudnessOffsetLu) < 1.0e-12,
                "real drop, selection and editor normalize a mono target to stereo A");
            if (! channelRoundTripped)
            {
                juce::MemoryBlock round;
                processor.getStateInformation (round);
                processor.setStateInformation (round.getData(), static_cast<int> (round.getSize()));
                require (processor.referenceTargetSnapshot().collection.items.front().channels == 1,
                         "processor state round trip retains the mono target metadata");
                channelRoundTripped = true;
                return false;
            }
            playing.store (false);
            require (processor.changeReferenceTarget ({ Action::select, {}, {} }), "return to audio C before queuing it");
            stage = 4; started = std::chrono::steady_clock::now(); return false;
        }
        if (stage == 4)
        {
            if (! runtime.checkSelection || runtime.checkSelection->transportPlaying) return false;
            if (! versionChecked)
            {
                if (runtime.versions.empty()) return false;
                require (processor.selectReferenceVersion (runtime.versions.front().id), "choose the real Version before queuing it");
                stage = 7; started = std::chrono::steady_clock::now(); return false;
            }
            if (! unreadableImportStarted)
            {
                const auto bad = root.getChildFile ("unreadable-target.wav");
                require (hypha::storage::writeText (bad, "not audio"), "unreadable product drop fixture");
                panel->filesDropped ({ bad.getFullPathName() }, 0, 0);
                unreadableImportStarted = true; return false;
            }
            if (processor.referenceTargetSnapshot().measuring) return false;
            require (processor.referenceTargetSnapshot().issue == "FILE UNREADABLE", "the real drop reports its failure");
            editor->setSize (300, 200);
            auto* shipping = dynamic_cast<KirinHyphaEditor*> (editor.get());
            require (shipping != nullptr, "the recovery uses the shipping editor");
            hypha::tests::target_editor::refresh (*shipping, {});
            require (panel->statusTextLayout (panel->footerStatusStrip().getLocalBounds()).line.text == "FILE UNREADABLE",
                     "the compact product initially shows the failed drop");
            require (processor.selectReferenceC() && processor.referenceAuditionSnapshot().pendingAudition.waiting(),
                     "the real C button queues a stopped audition");
            hypha::tests::target_editor::refresh (*shipping, {});
            const auto line = panel->statusTextLayout (panel->footerStatusStrip().getLocalBounds()).line;
            require (line.text == "FILE UNREADABLE / " + hypha::reference_ui::referenceStatusLine (panel->state()).text,
                     "the actual C entrance retains the failed-drop reason with its current audition status");
            const hypha::text_style::ShownTextLog log;
            juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
            juce::Graphics graphics (image); editor->paintEntireComponent (graphics, true);
            bool painted = false;
            for (const auto& text : log.texts())
                painted = painted || text.contains (hypha::text_style::shownText ("FILE UNREADABLE"));
            require (painted && processor.referenceAuditionSnapshot().pendingAudition.waiting(),
                     "the shipping editor paints the async drop failure without cancelling the queued audition");
            require (root.getChildFile ("long-check.wav").appendText ("changed"), "change the queued C fixture source");
            playing.store (true);
            stage = 5; started = std::chrono::steady_clock::now(); return false;
        }
        if (stage == 7)
        {
            if (! runtime.versionArmable) return false;
            require (processor.selectReferenceVisualSlot (1) && processor.selectReferenceB(),
                     "the real V entrance queues a stopped Version");
            require (processor.selectReferenceVersion (runtime.versions.front().id)
                && processor.referenceAuditionSnapshot().switchingSlot == 1, "reselecting the queued Version waits for its switch");
            auto* shipping = dynamic_cast<KirinHyphaEditor*> (editor.get());
            require (shipping && runtime.visualTimeline, "Version switch uses the shipping editor and a live A snapshot");
            auto previous = std::make_shared<hypha::reference_audition::VisualTimeline> (*runtime.visualTimeline);
            previous->aTargetLoudness.lufs = -60; previous->aTargetLoudness.maxTruePeak = -50;
            previous->aTargetTruePeak = -50; previous->aTickChannels = 1;
            hypha::tests::target_editor::holdTimeline (*shipping, previous, {});
            hypha::tests::target_editor::refresh (*shipping, {});
            require (panel->state().comparisonSlot == 1 && panel->state().visualTimeline == previous,
                     "V keeps its prior display while the real switch waits");
            require (processor.changeReferenceTarget ({ Action::select, {}, selected }), "choose numeric C while V is switching");
            hypha::tests::target_editor::refresh (*shipping, {});
            const auto& shown = panel->state();
            const auto current = shown.visualTimeline;
            require (shown.targetSelected && shown.switchingSlot == 1 && current && current != previous
                && shown.aKirin == current->aKirin && current->aTickChannels == 2,
                "numeric C uses its current A spectrum and channel layout instead of V's held snapshot");
            const auto levels = hypha::reference_audition::targetWindowLevels (*current);
            require (std::isfinite (levels.lufs) && std::abs (levels.lufs + 60) > 1
                && std::abs (shown.aIntegratedLoudness - levels.lufs) < 1.0e-9
                && std::abs (shown.aMaximumTruePeakDbtp - levels.maxTruePeak) < 1.0e-9
                && std::abs (shown.cueLoudnessOffsetLu - 10.0 * std::log10 (2.0)) < 1.0e-12,
                "the shipping numeric comparison takes LUFS, TP and channel normalization from that same A snapshot");
            processor.selectReferenceA();
            require (processor.changeReferenceTarget ({ Action::select, {}, {} }), "return to audio C after the display regression");
            versionChecked = true; stage = 4; started = std::chrono::steady_clock::now(); return false;
        }
        if (stage == 5)
        {
            if (runtime.pendingAudition.waiting()) return false;
            require (runtime.pendingAudition.slot == 2
                && runtime.pendingAudition.stage == hypha::reference_audition::PendingAuditionView::Stage::sourceChanged,
                "the queued C ends with a visible source failure");
            require (processor.changeReferenceTarget ({ Action::select, {}, selected }), "choose a numeric target after the C failure");
            stage = 6; started = std::chrono::steady_clock::now(); return false;
        }
        if (stage == 6)
        {
            if (! panel->state().targetSelected) return false;
            const auto line = hypha::reference_ui::referenceStatusLine (panel->state());
            require (runtime.pendingAudition.stage == hypha::reference_audition::PendingAuditionView::Stage::none
                && line.kind == hypha::reference_ui::StatusKind::ready && line.text == "DATA ONLY",
                "the shipping editor replaces the failed C recovery request with DATA ONLY");
            juce::XmlElement old ("KirinHyphaState");
            juce::MemoryBlock oldData; juce::AudioProcessor::copyXmlToBinary (old, oldData);
            processor.setStateInformation (oldData.getData(), static_cast<int> (oldData.getSize()));
            processor.editorBeingDeleted (editor.get());
            editor.reset();
            return true;
        }
        return false;
    }
private:
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::MemoryBlock saved;
    juce::String selected;
    std::uint64_t openedEpoch = 0;
    bool windowChosen = false, compactRequested = false, expanded = false;
    bool presetChosen = false, channelRoundTripped = false;
    bool versionChecked = false, unreadableImportStarted = false;
    int stage = 0;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    static hypha::reference_ui::Component* findPanel (juce::Component& root)
    {
        if (auto* panel = dynamic_cast<hypha::reference_ui::Component*> (&root)) return panel;
        for (auto* child : root.getChildren()) if (auto* panel = findPanel (*child)) return panel;
        return nullptr;
    }
    static void writeLongCheck (const juce::File& root)
    {
        require (root.getChildFile ("library").createDirectory().wasOk(), "long Check library fixture");
        const auto file = root.getChildFile ("long-check.wav");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (file.createOutputStream().release(), 48000, 2, 24, {}, 0));
        juce::AudioBuffer<float> second (2, 48000);
        for (int n = 0; n < 48000; ++n)
            for (int c = 0; c < 2; ++c) second.setSample (c, n, 0.01f * static_cast<float> (std::sin (6.283185307179586 * n / 48.0)));
        require (writer != nullptr, "long Check writer");
        for (int s = 0; s < 30; ++s) require (writer->writeFromAudioSampleBuffer (second, 0, 48000), "long Check PCM");
        writer.reset();
        const auto hash = juce::SHA256 (file).toHexString(), pcm = juce::String::repeatedString ("7", 64);
        auto source = makeRuntimeV2Source (file, hash, pcm);
        source["audio"].getDynamicObject()->setProperty ("total_sample_frames", static_cast<juce::int64> (48000 * 30));
        addRuntimeV2MeasurementSummary (source, -40.0, -40.0);
        const auto receipt = stageRuntimeV2Artifact (root, "sources", source);
        auto preset = bindRuntimeV2PresetToSource ("88888888-8888-4888-8888-888888888884", "99999999-9999-4999-8999-999999999994", receipt, hash, pcm);
        auto* object = preset.getDynamicObject();
        object->setProperty ("format", "kirin_hypha_reference_library_preset"); object->setProperty ("version", "1.0");
        object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
        auto candidate = preset["checks"][0]["candidates"][0];
        candidate.getDynamicObject()->setProperty ("preparation_status", "prepared");
        candidate["cues"][0].getDynamicObject()->setProperty ("start_sample", 0);
        candidate["cues"][0].getDynamicObject()->setProperty ("end_sample", 48000 * 30);
        auto manifest = libraryManifest (root, preset, 1);
        const juce::String recording = "22222222-2222-4222-8222-222222222222", version = "33333333-3333-4333-8333-333333333333";
        const auto fixture = makeWholeSongFixture (root, root.getChildFile ("target-version.wav"), recording, version);
        auto versionPreset = bindRuntimeV2WorkVersionPresetToSource ("88888888-8888-4888-8888-888888888888", "99999999-9999-4999-8999-999999999999",
            fixture.receipt, juce::SHA256 (root.getChildFile ("target-version.wav")).toHexString(), fixture.pcmHash,
            recording, version, fixture.audio.getNumSamples());
        auto* versionObject = versionPreset.getDynamicObject();
        versionObject->setProperty ("format", "kirin_hypha_reference_library_preset"); versionObject->setProperty ("version", "1.0");
        versionObject->removeProperty ("work_id"); versionObject->removeProperty ("source_preset_artifact");
        versionPreset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
        const auto versions = independentLibraryManifest (root, versionPreset, 2);
        manifest.getDynamicObject()->setProperty ("version", "1.1");
        manifest.getDynamicObject()->setProperty ("versions", versions["versions"]);
        require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "long Check and Version publication");
    }
};

inline void verifyUnlicensedTargetEntry()
{
    KirinHyphaProcessorBase processor (KirinHyphaProcessorBase::Role::Post);
    hypha::tests::prepareFixtureProcessor (processor, 48000, 480);
    processor.refreshLicenseForUserAction();
    require (! processor.licenseIsOs(), "target license fixture starts without a Kirin OS license");
    using Action = hypha::reference_audition::TargetRequest::Action;
    require (! processor.changeReferenceTarget ({ Action::make, {}, {} })
        && ! processor.changeReferenceTarget ({ Action::importFiles, { "/fixture/audio.wav" }, {} })
        && processor.referenceTargetSnapshot().collection.items.empty(),
        "actual processor actions retain the Reference entitlement boundary");
}
