#include "reference_runtime_test_support.h"
#include "reference_runtime_test_entries.h"
#include "../src/reference_audition/ReferenceContentAlignment.h"
#include "../src/reference_audition/ReferenceContentCorrelation.h"
#include "../src/reference_audition/ReferenceProbeAudio.h"
#include "kirin_hypha_ffi.h"

void testReferenceRealContentAlignment()
{
    const auto rootPath = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_ALIGNMENT_ROOT", {});
    if (rootPath.isEmpty()) return;
    const auto filename = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_ALIGNMENT_FILE", {});
    const juce::File variants (juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_ALIGNMENT_VARIANTS", {}));
    const juce::File root (rootPath);
    const auto workspace = ref::RuntimeV2Repository (root).refreshLibrary();
    require (workspace.usable(), "real alignment requires a verified OS library");
    ref::RuntimeV2SourceRepository repository (root);
    std::shared_ptr<const ref::RuntimeSource> selected;
    for (const auto& preset : workspace.workspace->presets)
        for (const auto& check : preset.checks)
            for (const auto& candidate : check.candidates)
            {
                if (!candidate.prepared) continue;
                const auto source = repository.load (candidate);
                if (source.accepted() && juce::File (source.source->absolutePath).getFileName() == filename)
                    selected = source.source;
            }
    require (selected != nullptr && repository.verifySourceFile (*selected).isEmpty(), "real source must have exact registered hashes");
    const auto measured = ref::RuntimeV2MeasurementRepository (root).load (*selected);
    require (measured.accepted(), "real alignment must reuse the bound OS measurement");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    for (const auto* variant : { "a", "gain", "eq", "dynamics", "unrelated" })
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (variants.getChildFile (juce::String (variant) + ".wav")));
        require (reader != nullptr, "private diagnostic variant must exist");
        for (const auto seconds : { 8, 91, 170 })
        {
            ref::RuntimeACaptureAudio a;
            a.sampleRateHz = selected->audio.sampleRateHz; a.channels = static_cast<int> (selected->audio.channels);
            a.startSample = a.sampleRateHz * 35; a.frameCount = a.sampleRateHz * 4;
            const auto expected = seconds * a.sampleRateHz;
            const auto padding = juce::String (variant) == "a" ? 0 : 317;
            require (ref::readReferenceProbe (*reader, expected + padding, a.frameCount,
                         static_cast<int> (a.sampleRateHz), a.channels, a.interleaved), "private probe must remain in real file bounds");
            a.cuePcmSha256 = ref::referenceProbePcmHash (a.interleaved);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            const auto match = ref::alignReferenceContent (a, *selected, *measured.measurement, false);
            std::cout << "real-content " << variant << " at " << seconds << "s accepted=" << match.established
                      << " error_samples=" << (match.established ? match.sourceStartSample - expected : 0)
                      << " correlation=" << match.minimumCorrelation << " elapsed_ms="
                      << juce::Time::getMillisecondCounterHiRes() - start << " reason=" << match.reason << std::endl;
            if (!match.established && juce::String (variant) != "unrelated")
            {
                std::unique_ptr<juce::AudioFormatReader> bReader (formats.createReaderFor (juce::File (selected->absolutePath)));
                for (const auto offset : { a.frameCount / 8, a.frameCount * 3 / 8, a.frameCount * 5 / 8 })
                {
                    const int count = static_cast<int> (a.sampleRateHz);
                    std::vector<float> raw, left (static_cast<size_t> (count)), right (static_cast<size_t> (count));
                    require (ref::readReferenceProbe (*bReader, expected + offset, count, count, a.channels, raw), "diagnostic probe bounds");
                    for (int frame = 0; frame < count; ++frame) {
                        left[static_cast<size_t> (frame)] = a.interleaved[static_cast<size_t> ((offset + frame) * a.channels)];
                        right[static_cast<size_t> (frame)] = raw[static_cast<size_t> (frame * a.channels)];
                    }
                    const auto diagnostic = ref::correlateReferenceContent (left, right, count, count / 4);
                    std::cout << "  exact-probe offset=" << diagnostic.offsetSamples << " score=" << diagnostic.correlation
                              << " ambiguity_db=" << diagnostic.ambiguityDb << std::endl;
                }
            }
            if (juce::String (variant) == "unrelated") require (!match.established, "unrelated audio must not acquire this song");
            else require (match.established && std::abs (match.sourceStartSample - expected) <= 1,
                          "real same-song variants must align within one sample at separated positions");
            if (match.established)
            {
                const auto cachedStart = juce::Time::getMillisecondCounterHiRes();
                const auto cached = ref::alignReferenceContent (a, *selected, *measured.measurement, false, match.sourceStartSample);
                require (cached.established && cached.sourceStartSample == match.sourceStartSample,
                    "cached real-song verification must not move the accepted position");
                std::cout << "  cached_ms=" << juce::Time::getMillisecondCounterHiRes() - cachedStart;
                KirinReferenceGainFacts gain {};
                require (kirin_hypha_analyze_reference_gain (a.interleaved.data(), match.alignedProbe.data(),
                    static_cast<size_t> (a.frameCount), static_cast<std::uint32_t> (a.sampleRateHz),
                    static_cast<std::uint32_t> (a.channels), &gain), "aligned real audio must provide paired loudness evidence");
                std::cout << " paired_delta_millilu=" << gain.paired_loudness_delta_median_millilu << std::endl;
                if (juce::String (variant) == "a" || juce::String (variant) == "gain")
                    require (std::abs (gain.paired_loudness_delta_median_millilu - (juce::String (variant) == "a" ? 0 : -6000)) <= 10,
                        "constant gain must recover its known level within 0.01 LU across the song");
            }
        }
    }
}
