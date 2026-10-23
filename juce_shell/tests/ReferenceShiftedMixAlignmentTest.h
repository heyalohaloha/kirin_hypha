#pragma once
#include "ReferenceVersionFormatFixture.h"
#include "../src/reference_audition/ReferenceContentAlignment.h"

inline void testReferenceShiftedMixAlignment (const juce::File& sandbox, int requestedRate = 0)
{
    for (int rate : {48000, 192000})
    {
        if (requestedRate != 0 && requestedRate != rate) continue;
        const auto root = sandbox.getChildFile ("shifted-mix-" + juce::String (rate));
        require (root.createDirectory().wasOk(), "isolated shifted mix directory");
        const auto base = makeWholeSongFixture (root, root.getChildFile ("input.wav"),
            "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333");
        const int delay = rate * 137 / 1000;
        const auto manifest = hypha::tests::version_formats::publish (root, base.audio, rate,
                                                                     rate == 48000, delay, 5, 0.06f);
        require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "publish shifted, edited Version");
        const auto library = ref::RuntimeV2Repository (root).refreshLibrary();
        require (library.usable(), "the edited Version is a valid library publication");
        std::shared_ptr<const ref::RuntimeSource> source;
        for (const auto& preset : library.workspace->presets)
            if (preset.versionEntry)
                source = ref::RuntimeV2SourceRepository (root).load (preset.checks.front().candidates.front()).source;
        require (source != nullptr, "decode a verified edited Version source");
        const auto measurement = ref::RuntimeV2MeasurementRepository (root).load (*source);
        require (measurement.accepted(), "the edited Version has bound measurement evidence");
        ref::RuntimeACaptureAudio a;
        a.sampleRateHz = 48000; a.channels = 2; a.frameCount = 192000; a.startSample = 48000 * 11;
        a.interleaved.resize (static_cast<size_t> (a.frameCount * 2));
        for (int frame = 0; frame < a.frameCount; ++frame)
            for (int ch = 0; ch < 2; ++ch)
                a.interleaved[static_cast<size_t> (frame * 2 + ch)] = base.audio.getSample (ch, frame);
        a.cuePcmSha256 = ref::referenceProbePcmHash (a.interleaved);
        const auto matched = ref::alignReferenceContent (a, *source, *measurement.measurement, true);
        if (!matched.established)
            std::cout << "shifted edited mix at " << rate << ": " << matched.reason << '\n';
        require (matched.established && std::abs (matched.sourceStartSample - delay) <= 1,
                 "a shifted same-song edit aligns by content despite a new musical layer");
        std::uint32_t unrelated = 461;
        for (auto& sample : a.interleaved)
        {
            unrelated = unrelated * 1664525u + 1013904223u;
            sample = (static_cast<float> (unrelated >> 8) / 16777216.0f - 0.5f) * 0.1f;
        }
        a.cuePcmSha256 = ref::referenceProbePcmHash (a.interleaved);
        require (!ref::alignReferenceContent (a, *source, *measurement.measurement, true).established,
                 "the same refinement cannot identify unrelated PCM as this song");
    }
}
