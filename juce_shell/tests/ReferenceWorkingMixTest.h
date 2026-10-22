#pragma once
#include "ReferenceVersionFormatFixture.h"
#include "ReferenceManualService.h"

inline void testReferenceWorkingMix (const juce::File& sandbox, bool stableProof = false, bool exactCopy = false)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile (stableProof ? (exactCopy ? "stable-copy-proof" : "stable-proof") : "working-mix");
    require (root.createDirectory().wasOk(), "isolated working-mix fixture");
    const auto base = makeWholeSongFixture (root, root.getChildFile ("input.wav"),
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333");
    const auto manifest = hypha::tests::version_formats::publish (root, base.audio, 48000, false, 0, 1);
    require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "publish the registered mix");
    const auto entry = manifest["versions"][0]["entry_id"].toString();
    const auto id = entry + "/" + entry + "/" + entry;
    ref::ReferenceComparisonController c (root);
    control::stopWorkers (c); c.configure ({"working-post", {}, 42, true}, 48000, 2);
    control::service (c); require (c.selectVersion (id), "choose a version of the working mix");
    c.setPresented (true); control::service (c);
    juce::AudioBuffer<float> input (2, 256);
    juce::String confirmedMap;
    const auto liveSample = [stableProof, exactCopy] (float sample, int channel) {
        return exactCopy ? sample : stableProof ? sample * (channel == 0 ? 1.001f : 0.999f)
                           : sample + 0.008f * std::tanh (sample * 18.0f);
    };
    for (int at = 0; at < base.audio.getNumSamples(); at += 256)
    {
        for (int ch = 0; ch < 2; ++ch) for (int f = 0; f < 256; ++f)
        {
            const auto sample = base.audio.getSample (ch, at + f);
            input.setSample (ch, f, liveSample (sample, ch));
        }
        c.observeTransport (at, true, true); c.observeAInput (input, at, true, true, true);
        if (at >= 192000)
            require (c.renderSelectedB (input, at, true, true, true),
                     "a working mix with different PCM keeps playing its selected Version");
        control::service (c); c.servicePendingAudition (-22, -6, true);
        if (at == 192000 - 256)
        {
            confirmedMap = control::roleFor (c, {1}).visualBinding().key;
            require (c.snapshot().versionReady && c.requestAudition (1, -22, -6),
                     "a small mix change still establishes the registered song");
        }
        if (stableProof && at >= 192000)
            require (control::roleFor (c, {1}).visualBinding().key == confirmedMap,
                     "fresh confirmation of the same map keeps the whole-song display identity and history");
        if (at >= 192000)
            require (c.snapshot().selectedVersionId == id
                && c.snapshot().pendingAudition.stage != ref::PendingAuditionView::Stage::sourceChanged,
                     "a non-identical working mix never becomes a source-file withdrawal");
    }
    if (exactCopy) { c.selectA(); return; }
    for (int ch = 0; ch < 2; ++ch) for (int f = 0; f < 256; ++f)
    {
        const auto sample = base.audio.getSample (ch, f);
        input.setSample (ch, f, liveSample (sample, ch));
    }
    c.observeTransport (0, true, true);
    control::service (c); // Refill the actual page before asking about content correspondence.
    c.observeAInput (input, 0, true, true, true);
    require (c.renderSelectedB (input, 0, true, true, true),
             "a seek in a working mix retains its established timeline correspondence");
    require (c.snapshot().selectedVersionId == id,
             "a working-mix seek never clears the explicit Version choice");
    c.selectA();
}
