#include "../src/reference_audition/ReferenceTonalRepository.h"
#include "reference_runtime_test_support.h"

#include <cmath>
#include <cstring>
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

juce::var encodedBytes (const juce::MemoryBlock& bytes, const char* encoding)
{
    auto value = new juce::DynamicObject();
    value->setProperty ("encoding", encoding);
    value->setProperty ("byte_count", static_cast<std::int64_t> (bytes.getSize()));
    value->setProperty ("data", juce::Base64::toBase64 (bytes.getData(), bytes.getSize()));
    return value;
}

juce::MemoryBlock tonalValues (size_t cells, float db)
{
    juce::MemoryBlock result (cells * 4, true);
    std::uint32_t word = 0;
    std::memcpy (&word, &db, sizeof db);
    auto* output = static_cast<std::uint8_t*> (result.getData());
    for (size_t cell = 0; cell < cells; ++cell)
    {
        output[cell * 4] = static_cast<std::uint8_t> (word);
        output[cell * 4 + 1] = static_cast<std::uint8_t> (word >> 8);
        output[cell * 4 + 2] = static_cast<std::uint8_t> (word >> 16);
        output[cell * 4 + 3] = static_cast<std::uint8_t> (word >> 24);
    }
    return result;
}

juce::var sourceTonalArtifact (const ref::RuntimeSource& source)
{
    constexpr double requests[] { 2.0, 0.5, 0.2 };
    constexpr double hops[] { 0.5, 0.2, 0.1 };
    constexpr std::int64_t fftSizes[] { 16384, 4096, 2048 };
    const auto rate = source.audio.sampleRateHz;
    const auto samples = source.audio.totalSampleFrames;
    const auto ratio = std::pow (1000.0, 1.0 / 60.0);
    std::array<juce::Array<juce::var>, 3> planeBands;
    juce::Array<juce::var> bands;
    for (int index = 0; index < 60; ++index)
    {
        const auto lower = 20.0 * std::pow (ratio, index);
        const auto upper = index == 59 ? 20000.0 : 20.0 * std::pow (ratio, index + 1);
        const auto center = std::sqrt (lower * upper);
        const auto plane = center < 160.0 ? 0 : center < 640.0 ? 1 : 2;
        auto band = new juce::DynamicObject();
        band->setProperty ("index", index); band->setProperty ("lower_hz", lower);
        band->setProperty ("upper_hz", upper); band->setProperty ("center_hz", center);
        band->setProperty ("plane_index", plane); bands.add (band); planeBands[size_t (plane)].add (index);
    }
    std::int64_t payloadBytes = 0;
    juce::Array<juce::var> planes;
    for (int planeIndex = 0; planeIndex < 3; ++planeIndex)
    {
        const auto fft = fftSizes[planeIndex];
        const auto hopSamples = static_cast<std::int64_t> (std::floor (hops[planeIndex] * rate + 0.5));
        const auto frames = samples < fft ? 0 : (samples - fft) / hopSamples + 1;
        const auto cells = frames * planeBands[size_t (planeIndex)].size();
        juce::Array<juce::var> frameTimes;
        for (std::int64_t frame = 0; frame < frames; ++frame)
            frameTimes.add (static_cast<double> (frame * hopSamples + fft / 2) / rate);
        auto validity = juce::MemoryBlock (static_cast<size_t> ((cells + 7) / 8), true);
        auto* validityBytes = static_cast<std::uint8_t*> (validity.getData());
        for (std::int64_t cell = 0; cell < cells; ++cell)
            validityBytes[cell / 8] |= static_cast<std::uint8_t> (1u << (cell % 8));
        const auto values = tonalValues (static_cast<size_t> (cells), -30.0f - planeIndex);
        payloadBytes += static_cast<std::int64_t> (values.getSize() + validity.getSize());
        auto plane = new juce::DynamicObject();
        plane->setProperty ("name", planeIndex == 0 ? "low" : planeIndex == 1 ? "low_mid" : "high");
        plane->setProperty ("window_request_sec", requests[planeIndex]);
        plane->setProperty ("window_sec", static_cast<double> (fft) / rate);
        plane->setProperty ("hop_sec", static_cast<double> (hopSamples) / rate);
        plane->setProperty ("fft_size", fft); plane->setProperty ("bin_hz", static_cast<double> (rate) / fft);
        plane->setProperty ("band_indices", planeBands[size_t (planeIndex)]);
        plane->setProperty ("frame_times_sec", frameTimes); plane->setProperty ("cell_count", cells);
        plane->setProperty ("valid_count", cells);
        plane->setProperty ("values", encodedBytes (values, "float32le-base64"));
        plane->setProperty ("validity", encodedBytes (validity, "bitset-lsb0-base64"));
        planes.add (plane);
    }
    auto identity = new juce::DynamicObject();
    identity->setProperty ("sha256_file", source.sourceFileSha256);
    identity->setProperty ("sha256_pcm", source.sourcePcmSha256);
    auto sourceFacts = new juce::DynamicObject();
    sourceFacts->setProperty ("sample_rate", rate); sourceFacts->setProperty ("channels", source.audio.channels);
    sourceFacts->setProperty ("sample_count", samples);
    sourceFacts->setProperty ("duration_sec", static_cast<double> (samples) / rate);
    sourceFacts->setProperty ("measurement_path", "decoded_source_pcm");
    auto tonal = new juce::DynamicObject();
    tonal->setProperty ("schema_version", "kirin_spectral_balance.v1");
    tonal->setProperty ("method_version", "kirin_multires_relative_power.v1");
    tonal->setProperty ("value_unit", "dB");
    tonal->setProperty ("value_definition", "10log10(band_power/same_aperture_20Hz_20kHz_power)");
    tonal->setProperty ("channel_aggregation", "channel_power_mean"); tonal->setProperty ("window_type", "hann");
    tonal->setProperty ("frequency_scale", "log_20Hz_20kHz_60_band");
    tonal->setProperty ("missing_policy", "validity_bitmap_no_interpolation");
    tonal->setProperty ("silence_gate_dbfs", -100.0); tonal->setProperty ("band_power_floor_db", -120.0);
    tonal->setProperty ("source", sourceFacts); tonal->setProperty ("payload_bytes", payloadBytes);
    tonal->setProperty ("bands", bands); tonal->setProperty ("planes", planes);
    auto artifact = new juce::DynamicObject();
    artifact->setProperty ("format", "kirin_hypha_reference_tonal"); artifact->setProperty ("version", "1.0");
    artifact->setProperty ("source_content", identity); artifact->setProperty ("tonal", tonal);
    return artifact;
}

void publishSourceTonal (const juce::File& root, const ref::RuntimeSource& source, const juce::var& artifact)
{
    const auto pending = root.getChildFile ("tonal-v1/artifacts/pending.json");
    require (pending.getParentDirectory().createDirectory().wasOk(), "source Balance directory is created");
    require (pending.replaceWithText (juce::JSON::toString (artifact, false)), "source Balance artifact is written");
    const auto hash = juce::SHA256 (pending).toHexString();
    const auto target = pending.getSiblingFile (hash + ".json");
    require (pending.moveFileTo (target), "source Balance artifact uses its content hash");
    auto identity = new juce::DynamicObject(); identity->setProperty ("sha256_file", source.sourceFileSha256);
    identity->setProperty ("sha256_pcm", source.sourcePcmSha256);
    auto item = new juce::DynamicObject(); item->setProperty ("source_content", identity);
    item->setProperty ("artifact", receipt ("tonal-v1/artifacts/" + hash + ".json", target));
    juce::Array<juce::var> sources; sources.add (item);
    auto genreReceipt = new juce::DynamicObject();
    genreReceipt->setProperty ("relative_path", "tonal-v1/genres/"
        + juce::String::repeatedString ("c", 64) + ".json");
    genreReceipt->setProperty ("sha256", juce::String::repeatedString ("c", 64));
    genreReceipt->setProperty ("bytes", 1);
    auto genre = new juce::DynamicObject(); genre->setProperty ("artifact", genreReceipt);
    genre->setProperty ("content_digest", juce::String::repeatedString ("d", 64));
    auto manifest = new juce::DynamicObject(); manifest->setProperty ("format", "kirin_hypha_reference_tonal_manifest");
    manifest->setProperty ("version", "1.0"); manifest->setProperty ("revision", 1);
    manifest->setProperty ("sources", sources); manifest->setProperty ("checks", juce::Array<juce::var>());
    manifest->setProperty ("genre_reference", genre);
    require (root.getChildFile ("tonal-v1/manifest.json").replaceWithText (
        juce::JSON::toString (juce::var (manifest), false)), "source Balance manifest is written");
}
}

void testReferenceTonalRepository (const juce::File& sandbox)
{
    {
        const auto sourceRoot = sandbox.getChildFile ("source-tonal-repository");
        ref::RuntimeSource source;
        source.sourceFileSha256 = juce::String::repeatedString ("a", 64);
        source.sourcePcmSha256 = juce::String::repeatedString ("b", 64);
        source.audio = { 48000, 2, 192000 };
        const auto producerFixture = juce::File (__FILE__).getParentDirectory()
            .getChildFile ("fixtures/reference_balance_wire_v1.json");
        require (producerFixture.existsAsFile()
            && juce::SHA256 (producerFixture).toHexString()
                == "f6345f3391d1d0000245bcd074ee13d2382c6a7593e9cdf43cad97711ffcb6e9",
            "the exact Kirin OS Balance wire fixture is available");
        auto valid = juce::JSON::parse (producerFixture);
        publishSourceTonal (sourceRoot, source, valid);
        ref::ReferenceTonalRepository sourceRepository (sourceRoot);
        const auto loaded = sourceRepository.load (source, 0, source.audio.totalSampleFrames);
        require (loaded && loaded->validBits == ((std::uint64_t (1) << 60) - 1),
                 "a fully verified source Balance artifact loads all sixty bands");
        const auto sourceManifest = juce::JSON::parse (sourceRoot.getChildFile ("tonal-v1/manifest.json"));
        const auto sourceReceipt = sourceManifest["sources"][0]["artifact"];
        const auto sourceHash = sourceReceipt["sha256"].toString();
        require (sourceRoot.getChildFile (sourceReceipt["relative_path"].toString()).deleteFile(),
                 "missing source Balance fixture is removed");
        bool retryable = false;
        require (! sourceRepository.load (source, 0, source.audio.totalSampleFrames, &retryable)
            && retryable
            && sourceRoot.getChildFile ("tonal-v1/repair-requests/" + sourceHash + ".json").existsAsFile(),
            "a missing current Balance produces one bounded automatic repair request");

        auto unknown = sourceTonalArtifact (source);
        unknown["tonal"].getDynamicObject()->setProperty ("unexpected", true);
        publishSourceTonal (sourceRoot, source, unknown);
        retryable = true;
        require (! sourceRepository.load (source, 0, source.audio.totalSampleFrames, &retryable)
            && ! retryable,
                 "an unknown source Balance field fails closed");

        auto hugeTime = sourceTonalArtifact (source);
        hugeTime["tonal"]["planes"][0]["frame_times_sec"].getArray()->set (0, 1.0e300);
        publishSourceTonal (sourceRoot, source, hugeTime);
        require (! sourceRepository.load (source, 0, source.audio.totalSampleFrames),
                 "a huge finite frame time is rejected before any integer conversion");

        auto badPayload = sourceTonalArtifact (source);
        badPayload["tonal"].getDynamicObject()->setProperty ("payload_bytes",
            static_cast<std::int64_t> (badPayload["tonal"]["payload_bytes"]) + 1);
        publishSourceTonal (sourceRoot, source, badPayload);
        require (! sourceRepository.load (source, 0, source.audio.totalSampleFrames),
                 "a mismatched Balance payload declaration fails closed");

        auto unknownManifest = juce::JSON::parse (sourceRoot.getChildFile ("tonal-v1/manifest.json"));
        unknownManifest.getDynamicObject()->setProperty ("unexpected", true);
        require (sourceRoot.getChildFile ("tonal-v1/manifest.json").replaceWithText (
            juce::JSON::toString (unknownManifest, false)), "unknown manifest field fixture is written");
        require (sourceRepository.publicationKey().isEmpty()
            && ! sourceRepository.load (source, 0, source.audio.totalSampleFrames),
            "an unknown manifest field cannot become a Balance publication");
    }
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
