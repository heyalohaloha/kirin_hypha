#pragma once
#include "reference_runtime_v2_analysis_test_support.h"
#include "../src/reference_audition/ReferenceProbeAudio.h"

namespace
{
    ref::RuntimeContentReceipt stageWholeSongArtifact (const juce::File& root, const juce::String& bucket, const juce::var& value)
    {
        const auto json = ref::RuntimeEventTransport::canonicalJson (value);
        const auto hash = juce::SHA256 (json.toRawUTF8(), json.getNumBytesAsUTF8()).toHexString();
        const auto file = root.getChildFile (bucket + "/" + hash + ".json");
        require (file.getParentDirectory().createDirectory().wasOk() && file.replaceWithText (json), "canonical whole-song source artifact");
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

    WholeSongFixture makeWholeSongFixture (const juce::File& root, const juce::File& file,
                                            const juce::String& recording, const juce::String& version)
    {
        WholeSongFixture result;
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
            result.audio.setSample (0, frame, value); result.audio.setSample (1, frame, -value);
        }
        {
            juce::WavAudioFormat format;
            auto stream = file.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
                stream.release(), 48000, 2, 32, {}, 0));
            require (writer && writer->writeFromAudioSampleBuffer (result.audio, 0, result.audio.getNumSamples()), "whole-song fixture audio");
        }
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        require (reader && reader->read (&result.audio, 0, result.audio.getNumSamples(), 0, true, true), "whole-song fixture readback");
        std::vector<float> pcm (static_cast<size_t> (result.audio.getNumSamples() * 2));
        for (int frame = 0; frame < result.audio.getNumSamples(); ++frame)
            for (int channel = 0; channel < 2; ++channel)
                pcm[static_cast<size_t> (frame * 2 + channel)] = result.audio.getSample (channel, frame);
        result.pcmHash = ref::referenceProbePcmHash (pcm);
        const auto hash = juce::SHA256 (file).toHexString();
        result.source = makeRuntimeV2WorkVersionSource (file, hash, result.pcmHash, recording, version, result.audio.getNumSamples());
        addRuntimeV2MeasurementSummary (result.source, -22, -6);
        auto measurement = makeRuntimeV2Measurement (hash, result.pcmHash);
        measurement["audio"].getDynamicObject()->setProperty ("total_sample_frames", result.audio.getNumSamples());
        auto* waveform = measurement["views"]["waveform"].getDynamicObject();
        waveform->setProperty ("frames_per_bin", 4800); waveform->setProperty ("bin_count", 80);
        juce::Array<juce::var> rms, peaks;
        for (int channel = 0; channel < 2; ++channel)
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
        const auto detail = stageWholeSongArtifact (root, "measurements", measurement);
        auto detailReceipt = new juce::DynamicObject();
        detailReceipt->setProperty ("relative_path", detail.relativePath);
        detailReceipt->setProperty ("sha256", detail.sha256); detailReceipt->setProperty ("bytes", detail.bytes);
        result.source["measurement"].getDynamicObject()->setProperty ("detail_artifact", juce::var (detailReceipt));
        result.receipt = stageWholeSongArtifact (root, "sources", result.source);
        return result;
    }

    [[maybe_unused]] void observeWholeSongFixture (ref::ReferenceComparisonController& controller,
                                  const WholeSongFixture& fixture, int& position)
    {
        constexpr int frames = 1024;
        if (position + frames > fixture.audio.getNumSamples()) position = 0;
        juce::AudioBuffer<float> input (2, frames);
        for (int channel = 0; channel < 2; ++channel)
            input.copyFrom (channel, 0, fixture.audio, channel, position, frames);
        controller.observeTransport (position, true, true);
        controller.observeAInput (input, position, true, true, true);
        position += frames;
    }

    [[maybe_unused]] void exerciseWholeSongLibraryTrial (ref::ReferenceComparisonController& controller,
                                        const WholeSongFixture& fixture, const juce::File& root)
    {
        controller.observeTransport (0, true, true);
        juce::Thread::sleep (200);
        require (controller.selectB (-22, -6), "normal Version B can lead directly into Blind");
        require (controller.startBlind (-22, -6), "whole-song Library Blind starts after acoustic confirmation");
        require (!controller.startBlind (-22, -6) && !controller.approveBlindLowerAAndStart (-22, -6),
            "repeated start actions cannot interrupt or replace the active trial");
        require (!controller.answerBlind (1), "an unheard trial cannot be answered");
        juce::AudioBuffer<float> input (2, 1024);
        for (int stimulus = 1; stimulus <= 2; ++stimulus)
        {
            if (stimulus == 2) require (controller.selectBlindStimulus (stimulus), "whole-song hidden stimulus switch");
            for (int callback = 0; callback < 155; ++callback)
            {
                const auto position = callback * input.getNumSamples();
                for (int channel = 0; channel < 2; ++channel)
                    input.copyFrom (channel, 0, fixture.audio, channel, position, input.getNumSamples());
                controller.observeTransport (position, true, true);
                controller.observeAInput (input, position, true, true, true);
                require (controller.renderSelectedB (input, position, true, true, true), "whole-song audition callback");
                juce::Thread::sleep (5);
            }
            controller.observeTransport (0, false, false);
            input.clear();
            require (controller.renderSelectedB (input, 0, false, true, true), "host pause without a sample clock preserves the whole-song trial");
            juce::Thread::sleep (600);
            require (controller.snapshot().blindPhase == ref::BlindPhase::active, "a paused trial survives the callback timeout");
            controller.observeTransport (0, true, true); juce::Thread::sleep (200);
        }
        require (controller.answerBlind (1) && controller.revealBlind(), "audible whole-song preference completes");
        controller.observeTransport (0, false, false);
        controller.endBlind();
        juce::Thread::sleep (300);
        require (controller.snapshot().blindPhase == ref::BlindPhase::inactive, "END during pause returns to the normal screen without another play click");
        int starts = 0, completions = 0;
        for (const auto& file : root.getChildFile ("library/events").findChildFiles (juce::File::findFiles, true, "*.json"))
        {
            const auto event = juce::JSON::parse (file);
            const auto type = event["event_type"].toString();
            if (type == "blind_compare_started") {
                ++starts;
                require (event["work_id"].isVoid() && event["payload"]["trial_start"]["sources"]["a"]["source_kind"] == "live_daw",
                         "whole-song journal never assigns B's Work to live A");
            }
            if (type == "blind_compare_completed") ++completions;
        }
        require (starts == 1 && completions == 1, "one immutable start/completion pair reaches the Library journal");
        const auto exportPath = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_WHOLE_SONG_EXPORT", {});
        if (exportPath.isNotEmpty())
            require (root.copyDirectoryTo (juce::File (exportPath)), "cross-language whole-song journal fixture export");
    }
}
