#pragma once
#include "reference_runtime_v2_analysis_test_support.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <cmath>
#include <limits>

namespace reference_switch_fixture
{
struct Song { juce::File file; juce::String hash, pcm, candidateId; ref::RuntimeContentReceipt source; };

// 2 秒の一定の値の曲。channels は 1 か 2、rate は元の音の sample rate。
Song writeSong (const juce::File& root, const juce::String& name, float level, double lufs, const juce::String& candidateId,
                int channels = 2, int rate = 48'000, int frames = 96'000, int bits = 32, float rightFactor = 1.0f)
{
    Song song { root.getChildFile (name + ".wav"), {}, juce::String::repeatedString (name.getLastCharacters (1), 64), candidateId, {} };
    {
        juce::WavAudioFormat format;
        auto stream = song.file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
            stream.release(), rate, static_cast<unsigned int> (channels), bits, {}, 0));
        constexpr int chunk = 8192;
        juce::AudioBuffer<float> data (channels, chunk);
        for (int channel = 0; channel < channels; ++channel)
            juce::FloatVectorOperations::fill (data.getWritePointer (channel), channel == 0 ? level : level * rightFactor, chunk);
        require (writer != nullptr, "fixture WAV writer");
        for (int offset = 0; offset < frames; offset += chunk)
            require (writer->writeFromAudioSampleBuffer (data, 0, std::min (chunk, frames - offset)), "song samples");
    }
    song.hash = juce::SHA256 (song.file).toHexString();
    auto source = makeRuntimeV2Source (song.file, song.hash, song.pcm);
    auto* audio = source["audio"].getDynamicObject();
    audio->setProperty ("channels", channels);
    audio->setProperty ("sample_rate_hz", rate);
    audio->setProperty ("total_sample_frames", frames);
    addRuntimeV2MeasurementSummary (source, std::isfinite (lufs) ? lufs : -18.0, -6.0);
    if (! std::isfinite (lufs)) source["measurement"].getDynamicObject()->setProperty ("summary", juce::var());
    song.source = stageRuntimeV2Artifact (root, "sources", source);
    return song;
}

juce::var candidate (const Song& song, const juce::String& name, const juce::String& cueId,
                     int cueStart = 0, int cueEnd = 96'000)
{
    auto* identity = new juce::DynamicObject();
    identity->setProperty ("catalog_reference_id", "catalog:source-test");
    identity->setProperty ("sha256_file", song.hash);
    identity->setProperty ("sha256_pcm", song.pcm);
    auto* artifact = new juce::DynamicObject();
    artifact->setProperty ("relative_path", song.source.relativePath);
    artifact->setProperty ("sha256", song.source.sha256);
    artifact->setProperty ("bytes", song.source.bytes);
    auto* cue = new juce::DynamicObject();
    cue->setProperty ("cue_id", cueId);
    cue->setProperty ("label", "Chorus");
    cue->setProperty ("sample_rate_hz", 48'000);
    cue->setProperty ("start_sample", cueStart);
    cue->setProperty ("end_sample", cueEnd);
    cue->setProperty ("loop_enabled", true);
    auto* object = new juce::DynamicObject();
    object->setProperty ("candidate_id", song.candidateId);
    object->setProperty ("display_name", name);
    object->setProperty ("source_kind", "catalog_track");
    object->setProperty ("source_identity", juce::var (identity));
    object->setProperty ("source_artifact", juce::var (artifact));
    object->setProperty ("cues", juce::Array<juce::var> { juce::var (cue) });
    object->setProperty ("default_cue_id", cueId);
    object->setProperty ("preparation_status", "prepared");
    return juce::var (object);
}

juce::var songSets (const juce::String& setId, const juce::Array<juce::var>& songs)
{
    auto* set = new juce::DynamicObject();
    set->setProperty ("song_set_id", setId);
    set->setProperty ("revision_id", "77777777-7777-4777-8777-777777777771");
    set->setProperty ("rank", 1);
    set->setProperty ("name", "Mastering refs");
    set->setProperty ("songs", songs);
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "kirin_hypha_reference_library_sets");
    root->setProperty ("version", "1.0");
    root->setProperty ("revision", 1);
    root->setProperty ("manifest_revision", 1);
    root->setProperty ("song_sets", juce::Array<juce::var> { juce::var (set) });
    root->setProperty ("check_sets", juce::Array<juce::var>());
    root->setProperty ("source_ranges", juce::Array<juce::var>());
    return juce::var (root);
}

juce::var libraryPreset (const Song& checkSong)
{
    auto preset = bindRuntimeV2PresetToSource ("88888888-8888-4888-8888-888888888888", "99999999-9999-4999-8999-999999999999",
                                               checkSong.source, checkSong.hash, checkSong.pcm);
    auto* object = preset.getDynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0");
    object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    return preset;
}


// thirdWithoutLevel：3 曲目は音量の値が無い（新しい MATCH を作れない曲）。
inline std::array<juce::String, 3> createLibrary (const juce::File& root, bool benchmark = false, bool thirdWithoutLevel = false,
                                                  bool fourthSong = false)
{
    require (root.createDirectory().wasOk(), "switch fixture directory");
    constexpr int rate = 48'000;
    const int frames = benchmark ? rate * 300 : rate * 2;
    const int bits = benchmark ? 24 : 32;
    const int start = benchmark ? rate * 60 : 0;
    const int end = start + rate * 2;
    const auto first = writeSong (root, "tone1", 0.1f, -18.0, "11111111-1111-4111-8111-111111111111", 2, rate, frames, bits);
    const auto second = writeSong (root, "tone2", 0.2f, -16.0, "22222222-2222-4222-8222-222222222222", 2, rate, frames, bits, -1.0f);
    const auto third = writeSong (root, "tone3", 0.3f, thirdWithoutLevel ? std::numeric_limits<double>::quiet_NaN() : -14.0, "33333333-3333-4333-8333-333333333333", 2, rate, frames, bits, -1.0f / 3.0f);
    juce::Array<juce::var> songs { candidate (first, "Fixture 1", "44444444-4444-4444-8444-444444444441", start, end),
                                   candidate (second, "Fixture 2", "44444444-4444-4444-8444-444444444442", start, end),
                                   candidate (third, "Fixture 3", "44444444-4444-4444-8444-444444444443", start, end) };
    if (fourthSong)
        songs.add (candidate (writeSong (root, "tone4", 0.4f, -14.0, "55555555-5555-4555-8555-555555555555", 2, rate, frames, bits, 0.5f),
                              "Fixture 4", "44444444-4444-4444-8444-444444444444", start, end));
    require (writeJson (root.getChildFile ("library/sets.json"), songSets ("66666666-6666-4666-8666-666666666661", songs))
        && writeJson (root.getChildFile ("library/manifest.json"), libraryManifest (root, libraryPreset (first), 1)),
        "a B set with three distinct fixture sources");
    return { first.hash, second.hash, third.hash };
}
}
