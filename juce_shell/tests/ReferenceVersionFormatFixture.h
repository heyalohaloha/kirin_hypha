#pragma once
#include "ReferenceWholeSongAudioFixture.h"
#include "reference_library_manifest_fixture.h"

namespace hypha::tests::version_formats
{
inline juce::var publish (const juce::File& root, const juce::AudioBuffer<float>& original,
                         int rate, bool flac, int delay, int number, float addedTone = 0.0f)
{
    const auto version = juce::String::repeatedString (juce::String (number), 8)
        + "-3333-4333-8333-333333333333";
    const juce::String recording = "22222222-2222-4222-8222-222222222222";
    const auto file = root.getChildFile ("version-" + juce::String (number) + (flac ? ".flac" : ".wav"));
    const int ratio = rate / 48000;
    juce::AudioBuffer<float> pcm (2, original.getNumSamples() * ratio + delay);
    pcm.clear();
    for (int c = 0; c < 2; ++c)
        for (int f = delay; f < pcm.getNumSamples(); ++f)
        {
            const double at = static_cast<double> (f - delay) / ratio;
            const int first = static_cast<int> (at), second = std::min (first + 1, original.getNumSamples() - 1);
            const auto a = original.getSample (c, first), b = original.getSample (c, second);
            const auto tone = addedTone * static_cast<float> (std::sin (
                juce::MathConstants<double>::twoPi * 659.255 * at / 48000.0 + c * 0.25));
            pcm.setSample (c, f, (a + (b - a) * static_cast<float> (at - first)) * 0.8f + tone);
        }
    {
        juce::WavAudioFormat wav; juce::FlacAudioFormat lossless;
        auto& format = flac ? static_cast<juce::AudioFormat&> (lossless) : static_cast<juce::AudioFormat&> (wav);
        auto output = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (output.release(), rate, 2, 24, {}, 0));
        require (writer && writer->writeFromAudioSampleBuffer (pcm, 0, pcm.getNumSamples()), "write isolated version format");
    }
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    require (reader && std::abs (reader->sampleRate - rate) < 0.001 && reader->lengthInSamples == pcm.getNumSamples()
        && reader->read (&pcm, 0, pcm.getNumSamples(), 0, true, true), "decode the complete registered version");
    std::vector<float> interleaved (static_cast<size_t> (pcm.getNumSamples() * 2));
    for (int f = 0; f < pcm.getNumSamples(); ++f)
        for (int c = 0; c < 2; ++c) interleaved[static_cast<size_t> (f * 2 + c)] = pcm.getSample (c, f);
    const auto hash = juce::SHA256 (file).toHexString(), pcmHash = ref::referenceProbePcmHash (interleaved);
    auto source = makeRuntimeV2WorkVersionSource (file, hash, pcmHash, recording, version, pcm.getNumSamples());
    source["audio"].getDynamicObject()->setProperty ("sample_rate_hz", rate);
    addRuntimeV2MeasurementSummary (source, -24, -6);
    auto measured = makeRuntimeV2Measurement (hash, pcmHash);
    measured["audio"].getDynamicObject()->setProperty ("sample_rate_hz", rate);
    measured["audio"].getDynamicObject()->setProperty ("total_sample_frames", pcm.getNumSamples());
    juce::Array<juce::var> rms, peaks;
    const int hop = rate / 10, bins = (pcm.getNumSamples() + hop - 1) / hop;
    for (int c = 0; c < 2; ++c)
    {
        juce::Array<juce::var> levels, maxima;
        for (int at = 0; at < pcm.getNumSamples(); at += hop)
        {
            const int count = std::min (hop, pcm.getNumSamples() - at);
            levels.add (static_cast<juce::int64> (std::llround (20000 * std::log10 (std::max (1.0e-12f, pcm.getRMSLevel (c, at, count))))));
            maxima.add (static_cast<juce::int64> (std::llround (20000 * std::log10 (std::max (1.0e-12f, pcm.getMagnitude (c, at, count))))));
        }
        rms.add (levels); peaks.add (maxima);
    }
    auto* wave = measured["views"]["waveform"].getDynamicObject();
    wave->setProperty ("frames_per_bin", hop); wave->setProperty ("bin_count", bins);
    wave->setProperty ("rms_millidbfs", rms); wave->setProperty ("sample_peak_millidbfs", peaks);
    measured["views"].getDynamicObject()->setProperty ("loudness", juce::var());
    const auto detail = stageWholeSongArtifact (root, "measurements", measured);
    auto* receipt = new juce::DynamicObject();
    receipt->setProperty ("relative_path", detail.relativePath); receipt->setProperty ("sha256", detail.sha256);
    receipt->setProperty ("bytes", detail.bytes);
    source["measurement"].getDynamicObject()->setProperty ("detail_artifact", juce::var (receipt));
    auto preset = bindRuntimeV2WorkVersionPresetToSource ("88888888-8888-4888-8888-888888888888",
        "99999999-9999-4999-8999-999999999999", stageWholeSongArtifact (root, "sources", source),
        hash, pcmHash, recording, version, pcm.getNumSamples());
    auto* object = preset.getDynamicObject(); object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0"); object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    auto candidate = preset["checks"][0]["candidates"][0];
    candidate.getDynamicObject()->setProperty ("preparation_status", "prepared");
    candidate["cues"][0].getDynamicObject()->setProperty ("sample_rate_hz", rate);
    return independentLibraryManifest (root, preset, number);
}
}
