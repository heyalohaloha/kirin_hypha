#include "reference_runtime_test_support.h"
#include "reference_runtime_test_entries.h"
#include "reference_rt_probe.h"
#include "../src/reference_audition/ReferenceContentAlignment.h"
#include "../src/reference_audition/ReferenceProbeAudio.h"
#include "../src/reference_audition/ReferenceRuntimeV2Blind.h"
#include "../src/reference_audition/ReferenceAudioPages.h"

void testReferenceContentAlignment (const juce::File& sandbox)
{
    const auto unicode = juce::JSON::parse (juce::String::fromUTF8 (R"({"name":"低音","literal":"\\u4f4e","line":"\n","value":-0.0})"));
    require (ref::RuntimeEventTransport::canonicalJson (unicode) == juce::String::fromUTF8 (
        R"({"line":"\n","literal":"\\u4f4e","name":"低音","value":0})"), "Japanese labels and literal escapes must have the same canonical bytes in C++ and Kirin OS");
    require (ref::RuntimeEventTransport::canonicalJson (juce::var (0.5)).isEmpty(), "event canonicalization rejects values outside its integer measurement schema");
    constexpr int rate = 16000, channels = 2, frames = rate * 60, delay = 317;
    std::vector<float> original (static_cast<size_t> (frames));
    std::uint32_t noise = 913, envelope = 867;
    float amplitude = 0.1f, filtered = 0.0f;
    for (int frame = 0; frame < frames; ++frame)
    {
        if (frame % 1600 == 0) {
            envelope = envelope * 1664525u + 1013904223u;
            amplitude = 0.03f + static_cast<float> (envelope >> 8) / 16777216.0f * 0.25f;
        }
        noise = noise * 1664525u + 1013904223u;
        const auto value = static_cast<float> (noise >> 8) / 16777216.0f - 0.5f;
        filtered += 0.7f * (value - filtered);
        original[static_cast<size_t> (frame)] = filtered * amplitude;
    }
    juce::AudioBuffer<float> buffer (channels, frames + delay);
    buffer.clear();
    for (int frame = 0; frame < frames; ++frame)
    {
        buffer.setSample (0, frame + delay, original[static_cast<size_t> (frame)]);
        buffer.setSample (1, frame + delay, -original[static_cast<size_t> (frame)]);
    }
    const auto file = sandbox.getChildFile ("content-song.wav");
    {
        auto output = file.createOutputStream();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (output.release(), rate, channels, 32, {}, 0));
        require (writer && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()), "content source must be written");
    }
    ref::RuntimeSource source;
    source.sourceKind = "work_version";
    source.absolutePath = file.getFullPathName();
    source.sourceFileSha256 = juce::SHA256 (file).toHexString();
    std::vector<float> wholePcm (static_cast<size_t> (buffer.getNumSamples() * channels));
    for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        for (int channel = 0; channel < channels; ++channel)
            wholePcm[static_cast<size_t> (frame * channels + channel)] = buffer.getSample (channel, frame);
    source.sourcePcmSha256 = ref::referenceProbePcmHash (wholePcm);
    source.measurementSummary.emplace();
    source.measurementSummary->maximumTruePeakDbtp = -6.0; // Conservative fixture ceiling.
    source.audio = { rate, channels, frames + delay };
    ref::RuntimeDetailedMeasurement measured;
    measured.sourceFileSha256 = source.sourceFileSha256;
    measured.sourcePcmSha256 = source.sourcePcmSha256;
    measured.audio = source.audio;
    measured.waveform.emplace();
    measured.waveform->framesPerBin = rate / 10;
    measured.waveform->rmsMillidbfs.resize (channels);
    for (int first = 0; first < buffer.getNumSamples(); first += rate / 10)
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto count = juce::jmin (rate / 10, buffer.getNumSamples() - first);
            double energy = 0.0;
            for (int i = 0; i < count; ++i) {
                const auto value = buffer.getSample (channel, first + i);
                energy += static_cast<double> (value) * value;
            }
            measured.waveform->rmsMillidbfs[static_cast<size_t> (channel)].push_back (
                static_cast<std::int64_t> (std::llround (10000.0 * std::log10 (juce::jmax (1.0e-30, energy / count)))));
        }
    constexpr int sourceStart = 17 * rate + 613;
    ref::RuntimeACaptureAudio a;
    a.sampleRateHz = rate; a.channels = channels; a.startSample = rate * 8;
    a.frameCount = rate * 4;
    a.interleaved.resize (static_cast<size_t> (a.frameCount * channels));
    for (std::int64_t frame = 0; frame < a.frameCount; ++frame)
        for (int channel = 0; channel < channels; ++channel)
            a.interleaved[static_cast<size_t> (frame * channels + channel)] =
                std::tanh (original[static_cast<size_t> (sourceStart + frame)] * 1.5f) * (channel == 0 ? 1.0f : -1.0f);
    a.dawRevisionId = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
    a.cuePcmSha256 = ref::referenceProbePcmHash (a.interleaved);
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto match = ref::alignReferenceContent (a, source, measured, false);
    std::cout << "content alignment " << match.established << " offset " << match.sourceStartSample
              << " expected " << sourceStart + delay << " score " << match.minimumCorrelation
              << " ambiguity " << match.minimumAmbiguityDb << " ms "
              << juce::Time::getMillisecondCounterHiRes() - started << '\n';
    require (match.established && std::abs (match.sourceStartSample - sourceStart - delay) <= 1,
             "OS measured RMS must locate nonlinear live A independently of DAW time without cancelling stereo anti-phase");
    require (match.alignedProbe.size() == a.interleaved.size(), "matched observation must retain complete calibrated sample bounds");
    ref::RuntimeV2Blind blind;
    require (blind.prepareWholeSong (a, source, match) && blind.snapshot().wholeSong,
             "verified content must prepare a whole-song comparison");
    ref::AudioPages pages;
    require (pages.open (source, rate, channels, false).isEmpty(), "streamed source must open");
    require (blind.start(), "whole-song comparison must start without replacing live A");
    constexpr int callbackFrames = 257;
    juce::AudioBuffer<float> live (channels, callbackFrames);
    const auto beyondProbe = match.sourceStartSample + rate * 10;
    for (int stimulus = 1; stimulus <= 2; ++stimulus)
    {
        if (stimulus == 2) require (blind.requestStimulus (2), "second stimulus must be selectable");
        for (int callback = 0; callback < 200; ++callback)
        {
            const auto sourcePosition = beyondProbe + callback * callbackFrames;
            pages.request (sourcePosition); pages.service();
            for (int channel = 0; channel < channels; ++channel)
                for (int frame = 0; frame < callbackFrames; ++frame) live.setSample (channel, frame, 0.12345f);
            beginReferenceRtProbe();
            const bool rendered = blind.render (live, a.startSample + rate * 10 + callback * callbackFrames,
                                                true, &pages, sourcePosition);
            const auto heapOperations = endReferenceRtProbe();
            require (rendered && heapOperations == 0, "whole-song streaming and switches must perform zero C++ heap operations on the callback");
        }
    }
    require (blind.answer (1) && blind.reveal(), "two heard stimuli must allow answer and reveal");
    const auto revealed = blind.snapshot();
    const int aStimulus = revealed.revealedStimulusOneSide == 0 ? 1 : 2;
    require (blind.requestStimulus (aStimulus), "live A must remain selectable");
    pages.request (beyondProbe); pages.service();
    for (int callback = 0; callback < 3; ++callback)
    {
        for (int channel = 0; channel < channels; ++channel)
            for (int frame = 0; frame < callbackFrames; ++frame) live.setSample (channel, frame, 0.23456f);
        require (blind.render (live, a.startSample + rate * 10, true, &pages, beyondProbe), "live A must render");
    }
    const float expectedLive = 0.23456f;
    require (std::memcmp (live.getReadPointer (0) + callbackFrames - 1, &expectedLive, sizeof (float)) == 0,
             "A must be the unchanged current DAW sample, never the frozen observation");
    const int bStimulus = aStimulus == 1 ? 2 : 1;
    require (blind.requestStimulus (bStimulus), "whole-song B remains selectable at boundaries");
    const auto endPosition = pages.lengthInSamples() - 100;
    pages.request (endPosition); pages.service();
    require (blind.render (live, a.startSample, true, &pages, endPosition), "a callback may cross the source end safely");
    require (std::memcmp (live.getReadPointer (0) + callbackFrames - 1, &expectedLive, sizeof (float)) == 0,
             "samples beyond the source end must remain live A");
    const auto beforeOutside = blind.snapshot();
    for (int channel = 0; channel < channels; ++channel)
        for (int frame = 0; frame < callbackFrames; ++frame) live.setSample (channel, frame, expectedLive);
    require (blind.render (live, a.startSample, true, &pages, pages.lengthInSamples() + rate), "outside-song transport preserves A");
    const auto outside = blind.snapshot();
    require (outside.activeStimulus == 0 && outside.stimulusOneAudibleFrames == beforeOutside.stimulusOneAudibleFrames
        && outside.stimulusTwoAudibleFrames == beforeOutside.stimulusTwoAudibleFrames, "unheard out-of-range B must not produce audible receipts");
    require (!blind.render (live, 0, false, &pages, 0), "missing clock never produces Reference audio");
    require (std::memcmp (live.getReadPointer (0), &expectedLive, sizeof (float)) == 0, "failed clock leaves A bit identical");
    blind.end();
    require (blind.renderInvalidatedA (live, true) && blind.completeNormalReturn(), "explicit normal return must be confirmed");
    pages.close();
    auto silence = a;
    std::fill (silence.interleaved.begin(), silence.interleaved.end(), 0.0f);
    const auto silenceMatch = ref::alignReferenceContent (silence, source, measured, false, match.sourceStartSample);
    require (!silenceMatch.established && silenceMatch.reason != "reference_alignment_content_changed",
             "silence cannot invent or contradict a previously accepted song map");
    auto unrelated = a;
    for (auto& value : unrelated.interleaved) {
        noise = noise * 1664525u + 1013904223u;
        value = static_cast<float> (noise >> 8) / 16777216.0f - 0.5f;
    }
    require (ref::alignReferenceContent (unrelated, source, measured, false, match.sourceStartSample).reason
        == "reference_alignment_content_changed", "three incompatible informative probes detect a changed A source");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    std::vector<float> invalidProbe;
    require (reader && !ref::readReferenceProbe (*reader, -1, 1024, rate * 2, channels, invalidProbe)
        && !ref::readReferenceProbe (*reader, reader->lengthInSamples - 10, 1024, rate * 2, channels, invalidProbe),
        "sample-rate conversion must reject missing source bounds rather than extrapolate a probe");
    auto resampled = a;
    resampled.sampleRateHz = 48000; resampled.startSample = 8 * 48000; resampled.frameCount = 4 * 48000;
    require (ref::readReferenceProbe (*reader, sourceStart + delay, resampled.frameCount, 48000, channels,
        resampled.interleaved), "approved source conversion must prepare a bounded observation");
    require (!ref::alignReferenceContent (resampled, source, measured, false).established,
        "different sample rates require explicit conversion approval");
    const auto converted = ref::alignReferenceContent (resampled, source, measured, true);
    require (converted.established && std::abs (converted.sourceStartSample - sourceStart - delay) <= 1,
        "approved conversion must preserve the measured source sample position");
    auto changed = measured;
    changed.sourceFileSha256 = juce::String::repeatedString ("f", 64);
    require (!ref::alignReferenceContent (a, source, changed, false).established, "stale measured source must be rejected");
    require (file.deleteFile(), "source removal must succeed");
    require (!ref::alignReferenceContent (a, source, measured, false).established, "missing source must reject without fabricating alignment");
    // Two identical choruses at different song positions cannot be disambiguated
    // by choosing the first one or trusting the DAW time hint.
    reader.reset();
    for (int channel = 0; channel < channels; ++channel)
        buffer.copyFrom (channel, 30 * rate, buffer, channel, 0, 30 * rate);
    for (auto& rms : measured.waveform->rmsMillidbfs)
        std::copy_n (rms.begin(), 300, rms.begin() + 300);
    {
        auto output = file.createOutputStream();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (output.release(), rate, channels, 32, {}, 0));
        require (writer && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()), "repeated source fixture must be written");
    }
    source.sourceFileSha256 = measured.sourceFileSha256 = juce::SHA256 (file).toHexString();
    for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        for (int channel = 0; channel < channels; ++channel)
            wholePcm[static_cast<size_t> (frame * channels + channel)] = buffer.getSample (channel, frame);
    source.sourcePcmSha256 = measured.sourcePcmSha256 = ref::referenceProbePcmHash (wholePcm);
    const auto repeated = ref::alignReferenceContent (a, source, measured, false);
    require (!repeated.established && repeated.reason == "reference_alignment_ambiguous",
        "identical repeated passages must wait for distinguishing content");
    testReferenceRealContentAlignment();
}
