#pragma once

#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_runtime_v2_analysis_test_support.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"

#include <cmath>

// A Kirin OS Reference library with one prepared B song (one set, one Full track cue), written into
// a test folder. The song is a sine of the given frequency and amplitude; its Kirin OS measurement
// summary is given, so a test chooses whether B's MATCH fits under the ceiling.
struct OneSongLibrary
{
    double frequencyHz = 440.0;
    float amplitude = 0.05f;
    double integratedLufs = -30.0, truePeakDbtp = -1.0;
    juce::String fileName { "quiet-peak.wav" }, displayName { "Quiet peak" };
};

inline void writeOneSongLibrary (const juce::File& root, const OneSongLibrary& song)
{
    require (root.getChildFile ("library").createDirectory().wasOk(), "sandbox Reference library folder");
    const auto file = root.getChildFile (song.fileName);
    {
        juce::WavAudioFormat format;
        auto stream = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.release(), 48'000, 2, 32, {}, 0));
        juce::AudioBuffer<float> data (2, 480'000);
        for (int frame = 0; frame < data.getNumSamples(); ++frame)
            for (int channel = 0; channel < 2; ++channel)
                data.setSample (channel, frame, song.amplitude
                    * static_cast<float> (std::sin (6.283185307179586 * song.frequencyHz * frame / 48'000.0)));
        require (writer && writer->writeFromAudioSampleBuffer (data, 0, data.getNumSamples()), "B song samples");
    }
    const auto hash = juce::SHA256 (file).toHexString();
    const auto pcm = juce::String::repeatedString ("7", 64);
    auto runtimeSource = makeRuntimeV2Source (file, hash, pcm);
    runtimeSource["audio"].getDynamicObject()->setProperty ("total_sample_frames", static_cast<juce::int64> (480'000));
    addRuntimeV2MeasurementSummary (runtimeSource, song.integratedLufs, song.truePeakDbtp);
    const auto source = stageRuntimeV2Artifact (root, "sources", runtimeSource);
    auto preset = bindRuntimeV2PresetToSource ("88888888-8888-4888-8888-888888888883", "99999999-9999-4999-8999-999999999993",
                                               source, hash, pcm);
    auto* object = preset.getDynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0");
    object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    auto* identity = new juce::DynamicObject();
    identity->setProperty ("catalog_reference_id", "catalog:source-test");
    identity->setProperty ("sha256_file", hash); identity->setProperty ("sha256_pcm", pcm);
    auto* artifact = new juce::DynamicObject();
    artifact->setProperty ("relative_path", source.relativePath);
    artifact->setProperty ("sha256", source.sha256); artifact->setProperty ("bytes", source.bytes);
    auto* cue = new juce::DynamicObject();
    cue->setProperty ("cue_id", "48484848-4848-4848-8848-484848484848"); cue->setProperty ("label", "Full track");
    cue->setProperty ("sample_rate_hz", 48'000); cue->setProperty ("start_sample", 0);
    cue->setProperty ("end_sample", 480'000); cue->setProperty ("loop_enabled", true);
    auto* entry = new juce::DynamicObject();
    entry->setProperty ("candidate_id", "59595959-5959-4959-8959-595959595959");
    entry->setProperty ("display_name", song.displayName); entry->setProperty ("source_kind", "catalog_track");
    entry->setProperty ("source_identity", juce::var (identity)); entry->setProperty ("source_artifact", juce::var (artifact));
    entry->setProperty ("cues", juce::Array<juce::var> { juce::var (cue) });
    entry->setProperty ("default_cue_id", "48484848-4848-4848-8848-484848484848");
    entry->setProperty ("preparation_status", "prepared");
    auto* set = new juce::DynamicObject();
    set->setProperty ("song_set_id", "69696969-6969-4969-8969-696969696969");
    set->setProperty ("revision_id", "7a7a7a7a-7a7a-4a7a-8a7a-7a7a7a7a7a7a");
    set->setProperty ("rank", 1); set->setProperty ("name", "Quiet refs");
    set->setProperty ("songs", juce::Array<juce::var> { juce::var (entry) });
    auto* sets = new juce::DynamicObject();
    sets->setProperty ("format", "kirin_hypha_reference_library_sets"); sets->setProperty ("version", "1.0");
    sets->setProperty ("revision", 1); sets->setProperty ("manifest_revision", 1);
    sets->setProperty ("song_sets", juce::Array<juce::var> { juce::var (set) });
    sets->setProperty ("check_sets", juce::Array<juce::var>()); sets->setProperty ("source_ranges", juce::Array<juce::var>());
    require (writeJson (root.getChildFile ("library/sets.json"), juce::var (sets))
                 && writeJson (root.getChildFile ("library/manifest.json"), libraryManifest (root, preset, 1)),
             "a library with one B song");
}
