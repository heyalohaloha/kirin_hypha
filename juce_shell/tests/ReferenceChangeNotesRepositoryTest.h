#pragma once
#include "ReferenceChangeNotesTest.h"

namespace
{
void verifyChangeNotesRepository (const juce::File& sandbox)
{
    namespace notes = hypha::tests::change_notes;
    const auto root = sandbox.getChildFile ("change-notes-repository");
    int checks = notes::verify (sandbox.getChildFile ("change-notes-reader"), require);
    const auto check = [&] (bool pass, const char* message) { ++checks; require (pass, message); };
    const auto model = notes::candidate();
    ref::RuntimeContentReceipt sourceReceipt { "plugin_data/reference/v2/sources/" + model.sourceArtifact.sha256 + ".json",
                                              model.sourceArtifact.sha256, 10 };
    auto preset = bindRuntimeV2WorkVersionPresetToSource (
        "88888888-8888-4888-8888-888888888888", "99999999-9999-4999-8999-999999999999",
        sourceReceipt, juce::String::repeatedString ("a", 64), juce::String::repeatedString ("b", 64),
        model.sourceRecordingId, model.sourceVersionId, 96000);
    auto* object = preset.getDynamicObject(); object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0"); object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    auto manifest = independentLibraryManifest (root, preset, 1);
    auto descriptor = juce::JSON::parse (root.getChildFile ("library/versions/" + manifest["versions"][0]["sha256"].toString() + ".json"));
    const auto entryId = descriptor["entry_id"].toString();
    auto body = notes::fixture(); body.getDynamicObject()->setProperty ("source_identity", descriptor["candidate"]["source_identity"].clone());
    body.getDynamicObject()->setProperty ("sample_rate_hz", descriptor["candidate"]["cues"][0]["sample_rate_hz"]);
    body["notes"][0].getDynamicObject()->setProperty ("at_seconds", 0);
    body["notes"][0].getDynamicObject()->setProperty ("at_sample", 0);
    const auto attachNotes = [&] {
        const auto r = stageWholeSongArtifact (root, "notes", body);
        auto* receipt = new juce::DynamicObject(); receipt->setProperty ("relative_path", r.relativePath);
        receipt->setProperty ("sha256", r.sha256); receipt->setProperty ("bytes", r.bytes);
        descriptor.getDynamicObject()->setProperty ("change_notes_artifact", juce::var (receipt));
    };
    const auto publish = [&] (int revision) {
        const auto r = stageWholeSongArtifact (root, "library/versions", descriptor);
        auto* receipt = manifest["versions"][0].getDynamicObject(); receipt->setProperty ("relative_path", r.relativePath);
        receipt->setProperty ("sha256", r.sha256); receipt->setProperty ("bytes", r.bytes);
        manifest.getDynamicObject()->setProperty ("revision", revision);
        require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "publish notes Version revision");
    };
    const auto selected = [&] (const ref::RuntimeWorkspaceLoadResult& loaded) -> const ref::RuntimePreset& {
        require (loaded.usable() && loaded.workspace != nullptr, "notes library remains usable");
        const auto found = std::find_if (loaded.workspace->presets.begin(), loaded.workspace->presets.end(),
            [&] (const auto& p) { return p.versionEntry && p.sourcePresetArtifact.presetId == entryId; });
        require (found != loaded.workspace->presets.end(), "notes Version remains present"); return *found;
    };
    ref::RuntimeV2Repository repository (root);
    attachNotes(); publish (1);
    const auto first = repository.refreshLibrary(); const auto& a = selected (first);
    check (a.changeNotesReceipt.has_value() && a.changeNotesIssue.isEmpty(), "valid notes receipt retained lazily");
    const auto one = ref::readChangeNotes (root, *a.changeNotesReceipt, a.checks[0].candidates[0]);
    check (one.notes && one.notes->notes[0].text.startsWith ("Check"), "selected notes read from actual Version receipt");
    body["notes"][0].getDynamicObject()->setProperty ("text", "A revised synthetic instruction."); attachNotes(); publish (2);
    const auto second = repository.refreshLibrary (first.workspace); const auto& b = selected (second);
    check (a.sourcePresetArtifact.presetId == b.sourcePresetArtifact.presetId
        && a.sourcePresetArtifact.revisionId == b.sourcePresetArtifact.revisionId
        && a.sourcePresetArtifact.sha256 != b.sourcePresetArtifact.sha256, "same Version identity new content receipt");
    const auto two = ref::readChangeNotes (root, *b.changeNotesReceipt, b.checks[0].candidates[0]);
    check (two.notes && two.notes->notes[0].text == "A revised synthetic instruction.", "updated Version resolves updated notes");
    check (one.notes && one.notes->notes[0].text.startsWith ("Check"), "old result immutable not mutated");
    descriptor.getDynamicObject()->removeProperty ("change_notes_artifact"); publish (3);
    const auto removed = repository.refreshLibrary (second.workspace); const auto& empty = selected (removed);
    check (! empty.changeNotesReceipt && empty.changeNotesIssue.isEmpty(), "removed field clears receipt without removing Version");
    descriptor.getDynamicObject()->setProperty ("change_notes_artifact", "invalid"); publish (4);
    const auto invalid = repository.refreshLibrary (removed.workspace); const auto& bad = selected (invalid);
    check (! bad.changeNotesReceipt && bad.changeNotesIssue.isNotEmpty()
        && bad.checks.size() == 1 && bad.checks[0].candidates.size() == 1, "malformed notes receipt leaves Version alive");
    body.getDynamicObject()->setProperty ("open_count", 999); attachNotes(); publish (5);
    const auto wrongBody = repository.refreshLibrary (invalid.workspace); const auto& rejected = selected (wrongBody);
    check (rejected.changeNotesReceipt.has_value(), "schema bad notes not eagerly read by repository");
    const auto noNotes = ref::readChangeNotes (root, *rejected.changeNotesReceipt, rejected.checks[0].candidates[0]);
    check (! noNotes.notes && noNotes.issue.isNotEmpty() && wrongBody.workspace->presets.size() == first.workspace->presets.size(),
        "bad body rejected independently while all library entries remain");
    check (checks > 0, "notes assertions actually ran");
    std::cout << "REFERENCE_CHANGE_NOTES_ASSERTIONS=" << checks << '\n';
}
}
