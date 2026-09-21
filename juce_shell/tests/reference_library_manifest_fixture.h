#pragma once
#include "../src/reference_audition/ReferenceLibraryVersions.h"
namespace {
juce::var libraryManifest (const juce::File& root, juce::var preset, std::int64_t revision)
{
    const auto* templateObject = preset["source_template_artifact"].getDynamicObject();
    const auto presetId = templateObject->getProperty ("preset_id");
    auto* item = new juce::DynamicObject();
    const auto json = ref::RuntimeEventTransport::canonicalJson (preset);
    const auto hash = juce::SHA256 (json.toRawUTF8(), json.getNumBytesAsUTF8()).toHexString();
    const auto presetFile = root.getChildFile ("library/presets/" + hash + ".json");
    require (presetFile.getParentDirectory().createDirectory().wasOk() && presetFile.replaceWithText (json), "canonical library preset must be written");
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
    const juce::var value (manifest);
    require (writeJson (root.getChildFile ("library/manifests/" + juce::String (revision) + ".json"), value), "immutable library manifest fixture");
    return value;
}
juce::var independentLibraryManifest (const juce::File& root, juce::var preset, std::int64_t revision)
{
    auto candidate = preset["checks"][0]["candidates"][0].clone();
    ref::RuntimeCandidate parsed;
    require (ref::runtime_repository_parsing::parseLibraryVersionCandidate (candidate, parsed), "valid measured Version candidate");
    const auto id = ref::referenceVersionEntryId (parsed);
    candidate.getDynamicObject()->setProperty ("candidate_id", id);
    candidate.getDynamicObject()->setProperty ("default_cue_id", id);
    candidate["cues"][0].getDynamicObject()->setProperty ("cue_id", id);
    candidate["cues"][0].getDynamicObject()->setProperty ("label", "Whole song");
    candidate["cues"][0].getDynamicObject()->setProperty ("loop_enabled", false);
    candidate["cues"][0].getDynamicObject()->setProperty ("start_sample", 0);
    auto* descriptor = new juce::DynamicObject();
    descriptor->setProperty ("format", "kirin_hypha_reference_library_version");
    descriptor->setProperty ("version", "1.0"); descriptor->setProperty ("entry_id", id);
    descriptor->setProperty ("display_name", candidate["display_name"]); descriptor->setProperty ("candidate", candidate);
    const auto artifact = stageWholeSongArtifact (root, "library/versions", juce::var (descriptor));
    auto* receipt = new juce::DynamicObject();
    receipt->setProperty ("entry_id", id); receipt->setProperty ("revision_id", id);
    receipt->setProperty ("relative_path", artifact.relativePath); receipt->setProperty ("sha256", artifact.sha256); receipt->setProperty ("bytes", artifact.bytes);
    auto cOnly = preset.clone(); cOnly["checks"].getArray()->remove (0);
    auto manifest = libraryManifest (root, cOnly, revision);
    manifest.getDynamicObject()->setProperty ("version", "1.1");
    manifest.getDynamicObject()->setProperty ("versions", juce::Array<juce::var> { juce::var (receipt) });
    require (writeJson (root.getChildFile ("library/manifests/" + juce::String (revision) + ".json"), manifest), "Version manifest fixture");
    return manifest;
}
}
