#pragma once

#include "kirin_hypha_ffi.h"
#include "kirin_hypha_capture_clock.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <cmath>
#include <memory>
#include <vector>

namespace update_integration
{
constexpr int rate = 48000, blockFrames = 4800, recordFrames = 4 * rate;
inline constexpr auto project = "update-fixture-project";
inline constexpr auto preId = "update-fixture-pre";
inline constexpr auto postId = "update-fixture-post";

inline juce::var object (std::initializer_list<std::pair<const char*, juce::var>> fields)
{
    auto* result = new juce::DynamicObject();
    for (const auto& field : fields) result->setProperty (field.first, field.second);
    return juce::var (result);
}

inline bool atomicJson (const juce::File& file, const juce::var& value)
{
    if (! file.getParentDirectory().createDirectory()) return false;
    juce::TemporaryFile temporary (file);
    return temporary.getFile().replaceWithText (juce::JSON::toString (value, true) + "\n", false, false, "\n")
        && temporary.overwriteTargetFileWithTemporary();
}

inline std::vector<float> audioBlock (float gain)
{
    std::vector<float> result (blockFrames * 2);
    for (int frame = 0; frame < blockFrames; ++frame)
    {
        const auto sample = gain * static_cast<float> (std::sin (
            juce::MathConstants<double>::twoPi * 1000.0 * frame / rate));
        result[static_cast<size_t> (frame * 2)] = sample;
        result[static_cast<size_t> (frame * 2 + 1)] = sample;
    }
    return result;
}

struct EngineDeleter { void operator() (KirinHypha* handle) const { kirin_hypha_destroy (handle); } };
using Engine = std::unique_ptr<KirinHypha, EngineDeleter>;

struct RecordPair
{
    RecordPair()
    {
        constexpr uint8_t roles[] { KIRIN_CHANNEL_ROLE_LEFT, KIRIN_CHANNEL_ROLE_RIGHT };
        pre.reset (kirin_hypha_create (rate, roles, 2));
        post.reset (kirin_hypha_create (rate, roles, 2));
        if (! pre || ! post) return;
        for (auto* handle : { pre.get(), post.get() })
        {
            kirin_hypha_set_license (handle, kirin_hypha_load_license());
            kirin_hypha_set_signal_state (handle, KIRIN_SIGNAL_STATE_ACTIVE);
        }
        kirin_hypha_set_identity (pre.get(), preId, project, "", "update-fixture");
        kirin_hypha_set_identity (post.get(), postId, project, "", "update-fixture");
        kirin_hypha_enable_pre_writes (pre.get());
        kirin_hypha_enable_post_writes (post.get());
        kirin_hypha_set_pair_target (post.get(), "update-fixture");
    }

    void heartbeat()
    {
        for (auto* handle : { pre.get(), post.get() })
            kirin_hypha_push_samples (handle, nullptr, 0, 2);
    }

    void drive (int blocks)
    {
        for (int block = 0; block < blocks; ++block)
        {
            push (pre.get(), preAudio);
            push (post.get(), postAudio);
            position += blockFrames;
            juce::Thread::sleep (100);
        }
    }

    void push (KirinHypha* handle, const std::vector<float>& audio)
    {
        const auto recording = kirin_hypha_is_recording (handle);
        kirin_hypha_note_record_block (handle, recording, recording, true, false,
                                       true, position, blockFrames);
        kirin_hypha_note_capture_window (handle, true, position, blockFrames,
            KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE, KIRIN_HYPHA_PRESENTATION_SOURCE_VST3,
            true, 0, true, 0, KIRIN_HYPHA_AUXILIARY_SOURCE_UNKNOWN, false, 0, false);
        kirin_hypha_push_samples (handle, audio.data(), blockFrames, 2);
    }

    Engine pre, post;
    std::vector<float> preAudio = audioBlock (0.5f), postAudio = audioBlock (0.25f);
    std::int64_t position = 0;
};

// The same real Drop wire contract as the parity capstone, with a real disposable WAV/hash.
inline bool publishDrop (const juce::File& data, const juce::var& signal)
{
    const auto wav = data.getParentDirectory().getChildFile ("update-fixture-bounce.wav");
    auto stream = wav.createOutputStream();
    if (! stream) return false;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
        stream.release(), rate, 2, 16, {}, 0));
    if (! writer) return false;
    juce::AudioBuffer<float> audio (2, recordFrames);
    const auto block = audioBlock (0.25f);
    for (int frame = 0; frame < recordFrames; ++frame)
        for (int channel = 0; channel < 2; ++channel)
            audio.setSample (channel, frame, block[static_cast<size_t> ((frame % blockFrames) * 2 + channel)]);
    if (! writer->writeFromAudioSampleBuffer (audio, 0, recordFrames)) return false;
    writer.reset();
    const auto now = juce::Time::currentTimeMillis();
    const auto session = signal["session_id"].toString();
    const auto generation = signal["capture_generation_id"].toString();
    const auto started = signal["generation_started_at_ms"];
    const auto hash = juce::SHA256 (wav).toHexString();
    const juce::String commitId = "update-fixture-drop", bounceId = "update-fixture-bounce";
    const auto sessions = juce::var (juce::Array<juce::var> { session });
    if (session.isEmpty() || generation.isEmpty()) return false;
    const auto metadata = object ({
        { "expected_duration_samples", juce::int64 (recordFrames) }, { "expected_sample_rate", rate },
        { "wav_path", wav.getFullPathName() }, { "bounce_id", bounceId }, { "created_at_ms", now },
        { "wav_file_size", wav.getSize() }, { "wav_mtime_ms", wav.getLastModificationTime().toMilliseconds() },
        { "wav_hash", hash } });
    const auto commit = object ({
        { "schema_version", "drop_record_commit.v1" }, { "drop_commit_id", commitId },
        { "project_hash", project }, { "record_session_id", session },
        { "capture_generation_id", generation }, { "generation_started_at_ms", started },
        { "created_at_ms", now }, { "metadata", metadata } });
    const auto transaction = object ({
        { "schema_version", "drop_record_transaction.v1" }, { "drop_commit_id", commitId },
        { "project_hash", project }, { "capture_generation_id", generation },
        { "generation_started_at_ms", started }, { "created_at_ms", now },
        { "bounce_id", bounceId }, { "wav_hash", hash }, { "record_session_ids", sessions } });
    const auto member = object ({ { "project_hash", project }, { "record_session_ids", sessions } });
    const auto generationTransaction = object ({
        { "schema_version", "drop_record_generation_transaction.v1" }, { "drop_commit_id", commitId },
        { "capture_generation_id", generation }, { "generation_started_at_ms", started },
        { "created_at_ms", now }, { "bounce_id", bounceId }, { "wav_hash", hash },
        { "projects", juce::Array<juce::var> { member } } });
    const auto expected = data.getChildFile (juce::String (project) + "/record_expected");
    return atomicJson (expected.getChildFile ("drop_commits/by_session/" + session + ".json"), commit)
        && atomicJson (expected.getChildFile ("drop_transactions/" + commitId + ".json"), transaction)
        && atomicJson (data.getChildFile ("capture_generation/drop_transactions/" + commitId + ".json"),
                       generationTransaction);
}
}
