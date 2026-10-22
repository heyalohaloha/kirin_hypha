#pragma once
#include "reference_runtime_v2_analysis_test_support.h"
#include "../src/reference_audition/ReferenceProbeAudio.h"
#include "../src/AtomicFile.h"
namespace
{
    constexpr int wholeSongObservationFrames = 1024;
    ref::RuntimeContentReceipt stageWholeSongArtifact (const juce::File& root, const juce::String& bucket, const juce::var& value)
    {
        const auto json = ref::RuntimeEventTransport::canonicalJson (value);
        const auto hash = juce::SHA256 (json.toRawUTF8(), json.getNumBytesAsUTF8()).toHexString();
        const auto file = root.getChildFile (bucket + "/" + hash + ".json");
        require (file.getParentDirectory().createDirectory().wasOk() && hypha::storage::writeText (file, json), "canonical whole-song source artifact");
        return { "plugin_data/reference/v2/" + bucket + "/" + hash + ".json", hash,
                 static_cast<std::int64_t> (json.getNumBytesAsUTF8()) };
    }
    struct WholeSongFixture
    {
        juce::AudioBuffer<float> audio { 2, 48000 * 8 };
        juce::String pcmHash;
        ref::RuntimeContentReceipt receipt;
        juce::var source;
    };

    [[maybe_unused]] WholeSongFixture makeWholeSongFixture (const juce::File& root, const juce::File& file,
                                            const juce::String& recording, const juce::String& version,
                                            int channels = 2, bool withLoudness = false)
    {
        WholeSongFixture result;
        result.audio.setSize (channels, result.audio.getNumSamples());
        std::uint32_t noise = 713, envelope = 239;
        float amplitude = 0.1f;
        for (int frame = 0; frame < result.audio.getNumSamples(); ++frame)
        {
            if (frame % 4800 == 0) {
                envelope = envelope * 1664525u + 1013904223u;
                amplitude = 0.03f + static_cast<float> (envelope >> 8) / 16777216.0f * 0.3f;
            }
            noise = noise * 1664525u + 1013904223u;
            const auto value = (static_cast<float> (noise >> 8) / 16777216.0f - 0.5f) * amplitude;
            result.audio.setSample (0, frame, value);
            if (channels == 2) result.audio.setSample (1, frame, -value);
        }
        {
            juce::WavAudioFormat format;
            auto stream = file.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
                stream.release(), 48000, static_cast<unsigned> (channels), 32, {}, 0));
            require (writer && writer->writeFromAudioSampleBuffer (result.audio, 0, result.audio.getNumSamples()), "whole-song fixture audio");
        }
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        require (reader && reader->read (&result.audio, 0, result.audio.getNumSamples(), 0, true, true), "whole-song fixture readback");
        std::vector<float> pcm (static_cast<size_t> (result.audio.getNumSamples() * channels));
        for (int frame = 0; frame < result.audio.getNumSamples(); ++frame)
            for (int channel = 0; channel < channels; ++channel)
                pcm[static_cast<size_t> (frame * channels + channel)] = result.audio.getSample (channel, frame);
        result.pcmHash = ref::referenceProbePcmHash (pcm);
        const auto hash = juce::SHA256 (file).toHexString();
        result.source = makeRuntimeV2WorkVersionSource (file, hash, result.pcmHash, recording, version, result.audio.getNumSamples());
        result.source["audio"].getDynamicObject()->setProperty ("channels", channels);
        addRuntimeV2MeasurementSummary (result.source, -22, -6);
        auto measurement = makeRuntimeV2Measurement (hash, result.pcmHash);
        measurement["audio"].getDynamicObject()->setProperty ("total_sample_frames", result.audio.getNumSamples());
        measurement["audio"].getDynamicObject()->setProperty ("channels", channels);
        auto* waveform = measurement["views"]["waveform"].getDynamicObject();
        waveform->setProperty ("frames_per_bin", 4800); waveform->setProperty ("bin_count", 80);
        juce::Array<juce::var> rms, peaks;
        for (int channel = 0; channel < channels; ++channel)
        {
            juce::Array<juce::var> levels, maxima;
            for (int first = 0; first < result.audio.getNumSamples(); first += 4800)
            {
                const auto rmsValue = result.audio.getRMSLevel (channel, first, 4800);
                const auto peak = result.audio.getMagnitude (channel, first, 4800);
                levels.add (static_cast<juce::int64> (std::llround (20000.0 * std::log10 (rmsValue))));
                maxima.add (static_cast<juce::int64> (std::llround (20000.0 * std::log10 (peak))));
            }
            rms.add (levels); peaks.add (maxima);
        }
        waveform->setProperty ("rms_millidbfs", rms); waveform->setProperty ("sample_peak_millidbfs", peaks);
        measurement["views"].getDynamicObject()->setProperty ("loudness", juce::var());
        if (withLoudness)
        {
            auto timeline = new juce::DynamicObject();
            timeline->setProperty ("start_sample", 0); timeline->setProperty ("hop_samples", 4800);
            juce::Array<juce::var> levels;
            for (int i = 0; i < 80; ++i) levels.add (-20000);
            timeline->setProperty ("lufs_m_millilu", levels); timeline->setProperty ("lufs_s_millilu", levels);
            measurement["views"].getDynamicObject()->setProperty ("loudness", juce::var (timeline));
        }
        const auto detail = stageWholeSongArtifact (root, "measurements", measurement);
        auto detailReceipt = new juce::DynamicObject();
        detailReceipt->setProperty ("relative_path", detail.relativePath);
        detailReceipt->setProperty ("sha256", detail.sha256); detailReceipt->setProperty ("bytes", detail.bytes);
        result.source["measurement"].getDynamicObject()->setProperty ("detail_artifact", juce::var (detailReceipt));
        result.receipt = stageWholeSongArtifact (root, "sources", result.source);
        return result;
    }

}
