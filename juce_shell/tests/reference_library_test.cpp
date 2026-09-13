#include "reference_runtime_v2_analysis_test_support.h"
#include <functional>
#include <cstring>

namespace
{
bool waitFor (ref::RuntimeV2Controller& controller, const std::function<bool(const ref::Snapshot&)>& predicate)
{
    for (int i = 0; i < 3000; ++i)
    {
        controller.observeTransport (0, true, true);
        if (predicate (controller.snapshot())) return true;
        juce::Thread::sleep (10);
    }
    const auto state = controller.snapshot();
    std::cerr << "Reference wait: " << static_cast<int> (state.state) << " " << state.rejectionCode
              << " / " << state.presetName << " / " << state.checkLabel << '\n';
    return false;
}

juce::var libraryManifest (const juce::File& root, juce::var preset, std::int64_t revision)
{
    const auto* templateObject = preset["source_template_artifact"].getDynamicObject();
    const auto presetId = templateObject->getProperty ("preset_id");
    auto* item = new juce::DynamicObject();
    const auto json = juce::JSON::toString (preset, true) + "\n";
    const auto hash = juce::SHA256 (json.toRawUTF8(), json.getNumBytesAsUTF8()).toHexString();
    require (writeJson (root.getChildFile ("library/presets/" + hash + ".json"), preset), "library preset must be written");
    item->setProperty ("preset_id", presetId);
    item->setProperty ("revision_id", templateObject->getProperty ("revision_id"));
    item->setProperty ("relative_path", "plugin_data/reference/v2/library/presets/" + hash + ".json");
    item->setProperty ("sha256", hash);
    item->setProperty ("bytes", static_cast<juce::int64> (json.getNumBytesAsUTF8()));
    auto* manifest = new juce::DynamicObject();
    manifest->setProperty ("format", "kirin_hypha_reference_library");
    manifest->setProperty ("version", "1.0");
    manifest->setProperty ("revision", revision);
    manifest->setProperty ("default_preset_id", presetId);
    manifest->setProperty ("presets", juce::var (juce::Array<juce::var> { juce::var (item) }));
    return juce::var (manifest);
}
}

void testReferenceLibraryContract (const juce::File& sandbox);
void testReferenceLibraryContract (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("independent-library");
    ref::RuntimeV2Repository repository (root);
    require (! repository.refreshLibrary().usable(), "no local Factory substitute may appear without OS data");
    ref::RuntimeIdentity identity { "library-post-one", {}, 42, true };
    require (identity.valid(), "a Reference receiver needs no Work binding");
    require (! ref::RuntimeIdentity { "library-post-one", workId, 42, true }.valid(),
             "library identity must not accept a fake Work");
    auto preset = makeRuntimeV2Preset ("88888888-8888-4888-8888-888888888888", "99999999-9999-4999-8999-999999999999");
    auto* object = preset.getDynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0");
    object->removeProperty ("work_id");
    object->removeProperty ("source_preset_artifact");
    auto* check = preset["checks"].getArray()->getReference (0).getDynamicObject();
    auto* candidate = check->getProperty ("candidates").getArray()->getReference (0).getDynamicObject();
    candidate->setProperty ("source_artifact", juce::var());
    candidate->setProperty ("preparation_status", "pending");
    auto manifest = libraryManifest (root, preset, 1);
    const auto file = root.getChildFile ("library/manifest.json");
    require (writeJson (file, manifest), "library manifest publication");
    auto first = repository.refreshLibrary();
    if (! first.usable()) std::cerr << first.rejectionCode << '\n';
    require (first.usable() && first.workspace->presets[0].checks[0].candidates.size() == 1,
             "unavailable audio must not erase saved candidates");
    {
        ref::RuntimeV2Controller controller (root);
        controller.configure (identity, 48000, 2);
        require (waitFor (controller, [] (const auto& s) { return s.libraryReceived && s.candidates.size() == 1; }),
                 "Work-less startup must receive selectors even when every candidate is unprepared");
        require (! controller.snapshot().bSelected && ! controller.selectB (-14, -2), "unprepared startup keeps A");
        auto buffer = juce::AudioBuffer<float> (2, 64);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 64; ++i) buffer.setSample (c, i, static_cast<float> (i) / 128);
        controller.renderSelectedB (buffer, 0, true, false);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 64; ++i)
        {
            const float expected = static_cast<float> (i) / 128;
            require (std::memcmp (buffer.getReadPointer (c, i), &expected, sizeof (float)) == 0,
                     "offline/unprepared A must stay bit-identical");
        }
    }
    auto conflict = manifest.clone();
    conflict.getDynamicObject()->setProperty ("default_preset_id", "11111111-1111-4111-8111-111111111111");
    require (writeJson (file, conflict), "same-revision conflict fixture");
    require (repository.refreshLibrary (first.workspace).rejectionCode == "reference_library_revision_conflict",
             "a changed body at the same revision must not replace accepted content");
    require (file.replaceWithText ("{"), "partial manifest fixture");
    require (repository.refreshLibrary (first.workspace).workspace == first.workspace, "partial writes retain immutable accepted data");
    require (! repository.refreshLibrary().usable(), "restart must reject partial data");
    check->setProperty ("candidates", juce::var (juce::Array<juce::var> {}));
    manifest = libraryManifest (root, preset, 2);
    require (writeJson (file, manifest), "empty check fixture");
    require (repository.refreshLibrary().usable(), "empty check remains a usable setting");
    const auto receipt = manifest["presets"].getArray()->getReference (0);
    root.getChildFile ("library/presets/" + receipt["sha256"].toString() + ".json").replaceWithText ("{}");
    require (! repository.refreshLibrary().usable(), "preset tampering must fail closed");
}

bool testReferenceLibraryOsFixture();
bool testReferenceLibraryOsFixture()
{
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_LIBRARY_FIXTURE", {});
    if (directory.isEmpty()) return false;
    const juce::File root (directory);
    ref::RuntimeV2Repository repository (root);
    const auto loaded = repository.refreshLibrary();
    if (! loaded.usable()) std::cerr << loaded.rejectionCode << '\n';
    require (loaded.usable(), "actual OS library must load without a Work");
    ref::RuntimeV2SourceRepository sourceRepository (root);
    std::set<juce::String> sourceHashes;
    for (const auto& preset : loaded.workspace->presets)
        for (const auto& check : preset.checks)
            for (const auto& candidate : check.candidates)
                if (candidate.prepared && sourceHashes.insert (candidate.sourceArtifact.sha256).second)
                {
                    const auto source = sourceRepository.load (candidate);
                    require (source.accepted(), "actual source identity must match candidate");
                    require (sourceRepository.verifySourceFile (*source.source).isEmpty(), "actual file hash and revision must match");
                    if (source.source->measurementArtifact)
                    {
                        const auto measured = ref::RuntimeV2MeasurementRepository (root).load (*source.source);
                        require (measured.accepted() && measured.measurement->waveform.has_value()
                            && measured.measurement->spectrum.has_value(), "OS measurements must reach Hypha with exact PCM binding");
                    }
                }
    ref::RuntimeV2Controller first (root), second (root);
    first.configure ({ "independent-post-one", {}, 42, true }, 44100, 2);
    second.configure ({ "independent-post-two", {}, 42, true }, 44100, 2);
    require (waitFor (first, [] (const auto& s) { return s.libraryReceived; })
        && waitFor (second, [] (const auto& s) { return s.libraryReceived; }), "two POSTs receive the same library automatically");
    const auto initial = second.snapshot();
    require (! first.snapshot().bSelected && ! initial.bSelected, "receipt does not select B");
    const auto other = initial.presets.front().id == initial.presetId ? initial.presets.back().id : initial.presets.front().id;
    require (first.selectPreset (other), "a preset is selected locally without OS requests");
    require (waitFor (first, [&] (const auto& s) { return s.presetId == other; }), "selected preset must appear even with no candidates");
    require (second.snapshot().presetId == initial.presetId, "POST choices must remain independent");
    if (! sourceHashes.empty())
    {
        bool testedAudio = false;
        for (const auto& preset : loaded.workspace->presets)
        {
            if (testedAudio) break;
            for (const auto& check : preset.checks)
            {
                if (testedAudio || check.candidates.empty() || ! check.candidates[0].prepared) continue;
                require (first.selectPreset (preset.sourcePresetArtifact.presetId), "select registered preset");
                require (waitFor (first, [&] (const auto& s) { return s.presetId == preset.sourcePresetArtifact.presetId; }), "preset arrival");
                require (first.selectCheck (check.checkId), "select registered check");
                require (waitFor (first, [&] (const auto& s) {
                    return s.checkId == check.checkId && (s.state == ref::RuntimeState::ready || s.sampleRateApprovalRequired);
                }), "registered source must prepare for audition");
                if (first.snapshot().sampleRateApprovalRequired)
                    require (first.approveSampleRateConversion(), "explicit sample-rate permission");
                require (waitFor (first, [] (const auto& s) { return s.state == ref::RuntimeState::ready && s.auditionBuffered; }), "source pages must be buffered");
                require (first.selectB (-14.0, -2.0), "explicit B must activate from prepared source");
                juce::AudioBuffer<float> buffer (2, 128); buffer.clear();
                bool audible = false;
                for (int block = 0; block < 100 && ! audible; ++block)
                {
                    first.observeTransport (block * 128, true, true);
                    first.renderSelectedB (buffer, block * 128, true, true);
                    audible = buffer.getMagnitude (0, 128) > 0.0f;
                    juce::Thread::sleep (10);
                }
                require (audible, "actual prepared audio must reach the output B buffer");
                const auto events = root.getChildFile ("library/events/independent-post-one");
                const auto completions = [&events] {
                    int count = 0;
                    for (const auto& file : events.findChildFiles (juce::File::findFiles, false, "*.json"))
                        if (juce::JSON::parse (file)["event_type"].toString() == "audition_completed") ++count;
                    return count;
                };
                const auto previousCompletions = completions();
                first.selectA();
                for (int c = 0; c < 2; ++c) for (int n = 0; n < 128; ++n) buffer.setSample (c, n, 0.125f);
                first.observeAInput (buffer, 12800, true, true, true);
                first.renderSelectedB (buffer, 12800, true, true);
                require (buffer.getSample (0, 0) == 0.125f && buffer.getSample (1, 127) == 0.125f,
                         "explicit return restores unmodified DAW A");
                first.configure ({ "independent-post-one", {}, 42, true }, 44100, 2);
                require (waitFor (first, [&] (const auto& s) { return s.checkId == check.checkId && s.state == ref::RuntimeState::ready; }), "host reprepare retains local preset selection");
                require (waitFor (first, [&] (const auto&) { return completions() > previousCompletions; }),
                         "confirmed A return must finish its journal across immediate host reprepare");
                require (! first.snapshot().bSelected, "host reprepare must not resume B");
                testedAudio = true;
            }
        }
        require (testedAudio, "at least one real source must be auditioned");
    }
    std::cout << "OS library -> two POSTs: " << loaded.workspace->presets.size() << " presets, "
              << sourceHashes.size() << " verified audio sources; default " << initial.presetName << '\n';
    return true;
}
