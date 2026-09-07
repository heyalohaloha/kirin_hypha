#pragma once

#include "reference_runtime_v2_manifest_test_support.h"

namespace
{
    [[maybe_unused]] juce::var makeRuntimeV2ProfileDistribution (int pointCount)
    {
        auto distribution = new juce::DynamicObject();
        juce::Array<juce::var> counts;
        juce::Array<juce::var> p10;
        juce::Array<juce::var> median;
        juce::Array<juce::var> p90;
        for (int index = 0; index < pointCount; ++index)
        {
            counts.add (static_cast<juce::int64> (3));
            p10.add (static_cast<juce::int64> (-25'000));
            median.add (static_cast<juce::int64> (-24'000));
            p90.add (static_cast<juce::int64> (-23'000));
        }
        distribution->setProperty ("contributor_count", juce::var (counts));
        distribution->setProperty ("p10", juce::var (p10));
        distribution->setProperty ("median", juce::var (median));
        distribution->setProperty ("p90", juce::var (p90));
        return juce::var (distribution);
    }

    [[maybe_unused]] juce::var makeRuntimeV2Profile (const juce::String& profileId,
                                    const juce::String& revisionId)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_hypha_reference_profile");
        root->setProperty ("version", "2.0");
        auto sourceProfile = new juce::DynamicObject();
        sourceProfile->setProperty ("profile_id", profileId);
        sourceProfile->setProperty ("revision_id", revisionId);
        sourceProfile->setProperty ("relative_path", "reference/profiles/" + profileId
                                                     + "/" + revisionId + ".v1.json");
        sourceProfile->setProperty ("sha256", juce::String::repeatedString ("a", 64));
        sourceProfile->setProperty ("bytes", static_cast<juce::int64> (4096));
        root->setProperty ("source_profile_artifact", juce::var (sourceProfile));
        root->setProperty ("name", "Modern Mixes");
        root->setProperty ("source_count", static_cast<juce::int64> (3));

        auto spectrum = new juce::DynamicObject();
        juce::Array<juce::var> centers;
        for (const auto center : { 20.0, 31.5, 50.0, 80.0, 125.0, 200.0,
                                   315.0, 500.0, 800.0, 1250.0, 2000.0, 3150.0 })
            centers.add (center);
        spectrum->setProperty ("band_centers_hz", juce::var (centers));
        spectrum->setProperty ("level_millidbfs", makeRuntimeV2ProfileDistribution (12));

        auto views = new juce::DynamicObject();
        views->setProperty ("waveform", juce::var());
        views->setProperty ("spectrum", juce::var (spectrum));
        views->setProperty ("loudness", juce::var());
        views->setProperty ("dynamics", juce::var());
        views->setProperty ("transient", juce::var());
        views->setProperty ("stereo", juce::var());
        root->setProperty ("views", juce::var (views));
        return juce::var (root);
    }

    juce::Array<juce::var> integerSeries (
        std::initializer_list<std::optional<std::int64_t>> values)
    {
        juce::Array<juce::var> result;
        for (const auto value : values)
            result.add (value ? juce::var (static_cast<juce::int64> (*value)) : juce::var());
        return result;
    }

    [[maybe_unused]] juce::var makeRuntimeV2Measurement (const juce::String& fileHash,
                                        const juce::String& pcmHash)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_hypha_reference_measurement");
        root->setProperty ("version", "2.0");
        auto content = new juce::DynamicObject();
        content->setProperty ("sha256_file", fileHash);
        content->setProperty ("sha256_pcm", pcmHash);
        root->setProperty ("source_content", juce::var (content));
        auto audio = new juce::DynamicObject();
        audio->setProperty ("sample_rate_hz", static_cast<juce::int64> (48'000));
        audio->setProperty ("channels", static_cast<juce::int64> (2));
        audio->setProperty ("total_sample_frames", static_cast<juce::int64> (96'000));
        root->setProperty ("audio", juce::var (audio));

        auto waveform = new juce::DynamicObject();
        waveform->setProperty ("start_sample", static_cast<juce::int64> (0));
        waveform->setProperty ("frames_per_bin", static_cast<juce::int64> (24'000));
        waveform->setProperty ("bin_count", static_cast<juce::int64> (4));
        juce::Array<juce::var> peaks, rms;
        for (int channel = 0; channel < 2; ++channel)
        {
            peaks.add (juce::var (integerSeries ({ -300'000, -300'000, -6'000, -5'000 })));
            rms.add (juce::var (integerSeries ({ -300'000, -300'000, -14'000, -13'000 })));
        }
        waveform->setProperty ("sample_peak_millidbfs", juce::var (peaks));
        waveform->setProperty ("rms_millidbfs", juce::var (rms));

        auto loudness = new juce::DynamicObject();
        loudness->setProperty ("start_sample", static_cast<juce::int64> (0));
        loudness->setProperty ("hop_samples", static_cast<juce::int64> (24'000));
        loudness->setProperty ("lufs_m_millilu", juce::var (integerSeries ({
            std::nullopt, std::nullopt, -14'000, -13'500 })));
        loudness->setProperty ("lufs_s_millilu", juce::var (integerSeries ({
            std::nullopt, std::nullopt, -14'500, -14'000 })));

        auto views = new juce::DynamicObject();
        views->setProperty ("waveform", juce::var (waveform));
        views->setProperty ("spectrum", juce::var());
        views->setProperty ("loudness", juce::var (loudness));
        views->setProperty ("dynamics", juce::var());
        views->setProperty ("transient", juce::var());
        views->setProperty ("stereo", juce::var());
        root->setProperty ("views", juce::var (views));
        return juce::var (root);
    }

    [[maybe_unused]] juce::var makeRuntimeV2Alignment (const juce::String& fileHash,
                                      const juce::String& pcmHash)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_hypha_reference_alignment");
        root->setProperty ("version", "2.0");
        root->setProperty ("feature_profile", "kirin_content_features_v1");
        auto content = new juce::DynamicObject();
        content->setProperty ("sha256_file", fileHash);
        content->setProperty ("sha256_pcm", pcmHash);
        root->setProperty ("source_content", juce::var (content));
        auto audio = new juce::DynamicObject();
        audio->setProperty ("sample_rate_hz", static_cast<juce::int64> (48'000));
        audio->setProperty ("channels", static_cast<juce::int64> (2));
        audio->setProperty ("total_sample_frames", static_cast<juce::int64> (96'000));
        root->setProperty ("audio", juce::var (audio));
        auto grid = new juce::DynamicObject();
        grid->setProperty ("start_sample", static_cast<juce::int64> (0));
        grid->setProperty ("hop_samples", static_cast<juce::int64> (24'000));
        grid->setProperty ("point_count", static_cast<juce::int64> (4));
        root->setProperty ("grid", juce::var (grid));

        auto features = new juce::DynamicObject();
        features->setProperty ("onset_strength_q15", juce::var (integerSeries ({ 0, 0, 1000, 800 })));
        for (const auto* name : { "sub_energy_millidbfs", "bass_energy_millidbfs",
                                  "mid_energy_millidbfs", "high_energy_millidbfs" })
            features->setProperty (name, juce::var (integerSeries ({
                std::nullopt, std::nullopt, -24'000, -23'000 })));
        juce::Array<juce::var> chroma;
        for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
            chroma.add (juce::var (integerSeries ({
                std::nullopt, std::nullopt, 100 + pitchClass, 200 + pitchClass })));
        features->setProperty ("chroma_q15", juce::var (chroma));
        features->setProperty ("loudness_millilu", juce::var (integerSeries ({
            std::nullopt, std::nullopt, -14'000, -13'500 })));
        root->setProperty ("features", juce::var (features));
        return juce::var (root);
    }

    [[maybe_unused]] ref::RuntimeContentReceipt stageRuntimeV2Artifact (const juce::File& v2Root,
                                                       const juce::String& kind,
                                                       const juce::var& artifact)
    {
        const auto pending = v2Root.getChildFile (kind).getChildFile ("pending.json");
        require (writeJson (pending, artifact), "runtime v2 artifact fixture must be written");
        const auto hash = juce::SHA256 (pending).toHexString();
        const auto exact = pending.getSiblingFile (hash + ".json");
        require (pending.moveFileTo (exact), "runtime v2 artifact must use its content hash");
        return {
            "plugin_data/reference/v2/" + kind + "/" + hash + ".json",
            hash,
            exact.getSize(),
        };
    }

    [[maybe_unused]] void testRuntimeACapture (const juce::File& v2Root)
    {
        const auto now = static_cast<std::int64_t> (1'788'390'000'000);
        const juce::String recordingId = "22222222-2222-4222-8222-222222222222";
        const ref::RuntimeABinding binding {
            "99999999-9999-4999-8999-999999999999",
            runtimeId,
            42,
            workId,
            recordingId,
            now,
            now + 9'000,
        };
        ref::RuntimeACapture capture (v2Root);
        capture.service (binding, 48'000, 2, now);

        juce::AudioBuffer<float> leadingSilence (2, 6'000);
        leadingSilence.clear();
        for (std::int64_t startSample = 0; startSample < 384'000;
             startSample += leadingSilence.getNumSamples())
        {
            capture.observe (leadingSilence, startSample, true, true, true);
            capture.service (binding, 48'000, 2, now + startSample / 6'000 + 1);
            require (! capture.currentReceipt().has_value(),
                     "four bars of intentional DAW silence must not become A content");
        }

        juce::AudioBuffer<float> content (2, 8'192);
        std::vector<float> expectedInterleaved;
        expectedInterleaved.reserve (192'000 * 2);
        std::int64_t sourceFrame = 0;
        for (int blockIndex = 0; blockIndex < 32 && ! capture.currentReceipt(); ++blockIndex)
        {
            for (int frame = 0; frame < content.getNumSamples(); ++frame)
            {
                const auto left = static_cast<float> (
                    0.2 * std::sin (juce::MathConstants<double>::twoPi
                                    * 997.0 * (sourceFrame + 0.25) / 48'000.0));
                const auto right = left * 0.75f;
                content.setSample (0, frame, left);
                content.setSample (1, frame, right);
                if (sourceFrame < 192'000)
                {
                    expectedInterleaved.push_back (left);
                    expectedInterleaved.push_back (right);
                }
                ++sourceFrame;
            }
            capture.observe (content, 384'000
                + static_cast<std::int64_t> (blockIndex) * content.getNumSamples(),
                true, true, true);
            capture.service (binding, 48'000, 2, now + 100 + blockIndex);
        }

        const auto captured = capture.currentReceipt();
        require (captured.has_value()
                 && captured->startSample == 384'000
                 && captured->frameCount == 192'000
                 && captured->sampleRateHz == 48'000
                 && captured->channels == 2,
                 "A receipt must preserve the sample-exact bar-five content start and four-second cue");
        juce::MemoryBlock canonical (expectedInterleaved.size() * sizeof (float), true);
        auto* output = static_cast<std::uint8_t*> (canonical.getData());
        for (size_t index = 0; index < expectedInterleaved.size(); ++index)
        {
            const float normalized = expectedInterleaved[index] == 0.0f
                ? 0.0f : expectedInterleaved[index];
            std::uint32_t bits = 0;
            std::memcpy (&bits, &normalized, sizeof (bits));
            output[index * 4] = static_cast<std::uint8_t> (bits & 0xffu);
            output[index * 4 + 1] = static_cast<std::uint8_t> ((bits >> 8u) & 0xffu);
            output[index * 4 + 2] = static_cast<std::uint8_t> ((bits >> 16u) & 0xffu);
            output[index * 4 + 3] = static_cast<std::uint8_t> ((bits >> 24u) & 0xffu);
        }
        require (captured->cuePcmSha256 == juce::SHA256 (canonical).toHexString(),
                 "A receipt hash must cover canonical interleaved Float32 LE content only");
        const auto capturedAudio = capture.currentAudio();
        require (capturedAudio != nullptr
                 && capturedAudio->dawRevisionId == captured->dawRevisionId
                 && capturedAudio->cuePcmSha256 == captured->cuePcmSha256
                 && capturedAudio->startSample == 384'000
                 && capturedAudio->frameCount == 192'000
                 && capturedAudio->interleaved == expectedInterleaved,
                 "A capture must expose the exact memory-only PCM to the Blind worker");

        const auto receiptFile = capture.captureFile (runtimeId);
        const auto receiptJson = juce::JSON::parse (receiptFile);
        require (receiptFile.existsAsFile() && receiptFile.getSize() <= 8'192
                 && receiptJson.getDynamicObject() != nullptr
                 && hasExactKeys (*receiptJson.getDynamicObject(), {
                     "format", "version", "binding_id", "runtime_instance_id",
                     "host_process_id", "work_id", "recording_id", "daw_revision_id",
                     "sample_rate_hz", "channels", "start_sample", "frame_count",
                     "cue_pcm_sha256", "captured_at_ms", "lease_expires_at_ms" })
                 && ! receiptJson.getDynamicObject()->hasProperty ("version_id")
                 && ! receiptJson.getDynamicObject()->hasProperty ("audio_path"),
                 "A capture must publish only the approved bounded fifteen-field receipt");
       #if ! JUCE_WINDOWS
        struct stat receiptInfo {};
        require (::lstat (receiptFile.getFullPathName().toRawUTF8(), &receiptInfo) == 0
                 && (receiptInfo.st_mode & 0777) == 0600,
                 "A capture receipt must be owner-only on POSIX");
       #endif

        const auto completedAt = captured->capturedAtMs;
        capture.observe (content, 384'000 + sourceFrame, true, true, true);
        capture.service (binding, 48'000, 2, now + 200);
        require (capture.currentReceipt().has_value()
                 && capture.currentReceipt()->capturedAtMs == completedAt,
                 "finishing inside a host block must preserve capture continuity");
        capture.observe (content, 384'000 + sourceFrame + content.getNumSamples(),
                         true, false, true);
        capture.service (std::nullopt, 48'000, 2, now + 201);
        require (! capture.currentReceipt().has_value() && ! receiptFile.exists(),
                 "lost A authority must remove the short-lived receipt without persisting audio");
        require (capture.currentAudio() == nullptr,
                 "lost A authority must also discard the memory-only PCM publication");
    }
}
