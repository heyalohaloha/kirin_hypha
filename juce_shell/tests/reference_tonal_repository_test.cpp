#include "../src/reference_audition/ReferenceTonalRepository.h"
#include "reference_runtime_test_support.h"

#include <cmath>
#include <functional>

namespace ref = hypha::reference_audition;

void testReferenceTonalRepository (const juce::File&);

namespace
{
juce::var numbers (int count, const std::function<double(int)>& value)
{
    juce::Array<juce::var> output;
    for (int index = 0; index < count; ++index) output.add (value (index));
    return output;
}

juce::var receipt (const juce::String& relative, const juce::File& file)
{
    auto value = new juce::DynamicObject();
    value->setProperty ("relative_path", relative);
    value->setProperty ("sha256", juce::SHA256 (file).toHexString());
    value->setProperty ("bytes", file.getSize());
    return value;
}

juce::var genreArtifact()
{
    const auto ratio = std::pow (1000.0, 1.0 / 60.0);
    auto profile = new juce::DynamicObject();
    profile->setProperty ("display_label", "Pop");
    profile->setProperty ("genre", "pop");
    profile->setProperty ("groups", new juce::DynamicObject());
    profile->setProperty ("p10", numbers (60, [] (int index) { return -50.0 + index * 0.1; }));
    profile->setProperty ("p50", numbers (60, [] (int index) { return -40.0 + index * 0.1; }));
    profile->setProperty ("p90", numbers (60, [] (int index) { return -30.0 + index * 0.1; }));
    profile->setProperty ("profile_version", "pop.runtime.v1");
    auto profiles = new juce::DynamicObject(); profiles->setProperty ("pop", profile);
    juce::Array<juce::var> groups;
    for (int index = 0; index < 4; ++index) groups.add (new juce::DynamicObject());
    auto bundle = new juce::DynamicObject();
    bundle->setProperty ("band_count", 60);
    bundle->setProperty ("band_edges", numbers (61, [ratio] (int index) {
        return index == 60 ? 20000.0 : 20.0 * std::pow (ratio, index);
    }));
    bundle->setProperty ("content_digest", juce::String::repeatedString ("c", 64));
    bundle->setProperty ("frequency_scale", "log_20Hz_20kHz_60_band");
    bundle->setProperty ("groups", groups);
    bundle->setProperty ("method_version", "kirin_multires_relative_power.v1");
    bundle->setProperty ("profiles", profiles);
    bundle->setProperty ("schema_version", "kirin_spectral_balance_genre_reference_runtime.v1");
    auto artifact = new juce::DynamicObject();
    artifact->setProperty ("format", "kirin_hypha_reference_genres");
    artifact->setProperty ("version", "1.0");
    artifact->setProperty ("bundle", bundle);
    return artifact;
}
}

void testReferenceTonalRepository (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("tonal-repository");
    const auto pending = root.getChildFile ("tonal-v1/genres/pending.json");
    require (pending.getParentDirectory().createDirectory().wasOk(), "genre directory is created");
    require (pending.replaceWithText (juce::JSON::toString (genreArtifact(), true)), "genre artifact is written");
    const auto hash = juce::SHA256 (pending).toHexString();
    const auto artifact = pending.getSiblingFile (hash + ".json");
    require (pending.moveFileTo (artifact), "genre artifact uses its content hash");
    const auto preset = "11111111-1111-4111-8111-111111111111";
    const auto revision = "22222222-2222-4222-8222-222222222222";
    const auto check = "33333333-3333-4333-8333-333333333333";
    auto setting = new juce::DynamicObject();
    setting->setProperty ("preset_id", preset); setting->setProperty ("preset_revision_id", revision);
    setting->setProperty ("check_id", check); setting->setProperty ("enabled", true);
    setting->setProperty ("genre_id", "pop");
    juce::Array<juce::var> checks; checks.add (setting);
    auto genre = new juce::DynamicObject();
    genre->setProperty ("artifact", receipt ("tonal-v1/genres/" + hash + ".json", artifact));
    genre->setProperty ("content_digest", juce::String::repeatedString ("c", 64));
    juce::var manifest { new juce::DynamicObject() };
    auto* manifestObject = manifest.getDynamicObject();
    manifestObject->setProperty ("format", "kirin_hypha_reference_tonal_manifest");
    manifestObject->setProperty ("version", "1.0"); manifestObject->setProperty ("revision", 1);
    manifestObject->setProperty ("sources", juce::Array<juce::var>());
    manifestObject->setProperty ("checks", checks); manifestObject->setProperty ("genre_reference", genre);
    const auto manifestFile = root.getChildFile ("tonal-v1/manifest.json");
    require (manifestFile.replaceWithText (juce::JSON::toString (manifest, true)), "genre manifest is written");
    ref::ReferenceTonalRepository repository (root);
    const auto firstPublication = repository.publicationKey();
    require (firstPublication.length() == 64, "a valid Tonal manifest has an exact publication key");
    const auto loaded = repository.loadGenre (preset, revision, check);
    require (loaded && loaded->genreId == "pop" && loaded->displayLabel == "Pop",
             "exact Preset revision and Check load the selected Genre distribution");
    require (loaded->validBits == ((std::uint64_t (1) << 60) - 1)
        && std::abs (loaded->median[12] + 38.8f) < 0.001f,
        "all sixty ordered Genre bands are verified without changing their scale");
    require (! repository.loadGenre (preset, revision, "44444444-4444-4444-8444-444444444444"),
             "another Check cannot inherit a Genre setting");
    manifestObject->setProperty ("revision", 2);
    require (manifestFile.replaceWithText (juce::JSON::toString (manifest, true)),
             "a newer Tonal publication is written");
    require (repository.publicationKey().length() == 64
        && repository.publicationKey() != firstPublication,
        "the publication key changes when Genre settings are republished");
    setting->setProperty ("enabled", 1);
    require (manifestFile.replaceWithText (juce::JSON::toString (manifest, true)),
             "invalid enabled type fixture is written");
    require (! repository.loadGenre (preset, revision, check),
             "a non-boolean enabled field cannot activate a Genre distribution");
    setting->setProperty ("enabled", true);
    require (manifestFile.replaceWithText (juce::JSON::toString (manifest, true)),
             "valid Genre manifest is restored");
    require (artifact.replaceWithText ("{}"), "genre corruption fixture is written");
    require (! repository.loadGenre (preset, revision, check),
             "a changed Genre artifact fails closed without affecting Reference audio");
}
