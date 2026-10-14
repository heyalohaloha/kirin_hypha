#pragma once
#include "../src/AtomicFile.h"
#include "../src/reference_audition/ReferencePresetAdoptionTransport.h"
#include "TestPathBudget.h"

namespace hypha::tests
{
namespace ref = hypha::reference_audition;
inline void verifyPresetAdoptionPublication (
    const juce::File& transportRoot, const ref::RuntimeIdentity& identity,
    const ref::RuntimeSourcePresetReceipt& sourceTemplate,
    const ref::RuntimeSourcePresetReceipt& sourcePreset, int pathLength = 0)
{
    const auto require = [] (bool ok, const char* why)
    { if (! ok) throw std::runtime_error (why); };
    auto root = transportRoot.getChildFile ("ad");
    const auto initialFile = ref::PresetAdoptionTransport (root).adoptionFile (identity, 4, sourcePreset);
    const auto padding = pathLength - initialFile.getFullPathName().length();
    if (padding > 0)
        root = root.getChildFile (juce::String::repeatedString ("p", std::max (1, padding - 1)));
    const ref::PresetAdoptionTransport transport (root);
    const auto file = transport.adoptionFile (identity, 4, sourcePreset);
    require (file.getFullPathName().length() >= pathLength,
             "adoption fixture must exercise a long destination path");
    // The destination fits a normal Windows path; appending a UUID to its name would not.
    require (transport.write (identity, 4, sourceTemplate, sourcePreset, 1000),
             "long-path adoption must publish the complete receipt");
    const auto original = file.loadFileAsString();
    const auto value = juce::JSON::parse (original);
    require (value.getDynamicObject() != nullptr
             && value["source_template_artifact"]["revision_id"] == sourceTemplate.revisionId
             && value["source_preset_artifact"]["revision_id"] == sourcePreset.revisionId,
             "long-path adoption must preserve both exact artifact revisions");
    require (transport.write (identity, 4, sourceTemplate, sourcePreset, 2000)
             && file.loadFileAsString() == original,
             "identical adoption retry must preserve the immutable receipt bytes");
    auto changed = sourceTemplate;
    changed.sha256 = juce::String::repeatedString (sourceTemplate.sha256 == juce::String::repeatedString ("a", 64)
                                                 ? "b" : "a", 64);
    require (! transport.write (identity, 4, changed, sourcePreset, 3000)
             && file.loadFileAsString() == original,
             "conflicting adoption must not replace the accepted receipt");
    const auto files = file.getParentDirectory().findChildFiles (juce::File::findFiles, false);
    require (files.size() == 1 && files[0] == file,
             "adoption publication and retry must leave no temporary files");

    const auto blockedRoot = transportRoot.getChildFile ("adoption-blocked");
    const auto obstruction = blockedRoot.getChildFile ("preset_adoptions");
    require (blockedRoot.createDirectory() && hypha::storage::writeText (obstruction, "keep"),
             "adoption parent obstruction must be staged");
    require (! ref::PresetAdoptionTransport (blockedRoot).write (identity, 4, sourceTemplate, sourcePreset, 1000)
             && obstruction.loadFileAsString() == "keep",
             "adoption publication failure must preserve the obstructing file");
    require (hypha::tests::removeFixtureDirectory (transportRoot.getChildFile ("ad"))
             && hypha::tests::removeFixtureDirectory (blockedRoot), "adoption fixtures must be removed");
}

}
