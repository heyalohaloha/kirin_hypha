#pragma once
#include "ReferenceVersionFormatFixture.h"
#include "ReferenceManualService.h"
#include "reference_rt_probe.h"

inline void testReferenceCheckSwitch (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile ("check-switch");
    require (root.createDirectory().wasOk(), "isolated Check switch fixture");
    const auto file = root.getChildFile ("constant.wav");
    juce::AudioBuffer<float> pcm (2, 96000);
    for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (pcm.getWritePointer (ch), 0.25f, pcm.getNumSamples());
    {
        auto output = file.createOutputStream(); juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (output.release(), 48000, 2, 32, {}, 0));
        require (writer && writer->writeFromAudioSampleBuffer (pcm, 0, pcm.getNumSamples()), "write audible C fixture");
    }
    const auto hash = juce::SHA256 (file).toHexString();
    const auto pcmHash = ref::referenceProbePcmHash (std::vector<float> (192000, 0.25f));
    auto source = makeRuntimeV2Source (file, hash, pcmHash);
    addRuntimeV2MeasurementSummary (source, -22, -6);
    auto preset = bindRuntimeV2PresetToSource ("88888888-8888-4888-8888-888888888888",
        "99999999-9999-4999-8999-999999999999", stageWholeSongArtifact (root, "sources", source), hash, pcmHash);
    auto* object = preset.getDynamicObject(); object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0"); object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    auto first = preset["checks"][0];
    first["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    first["candidates"][0]["cues"][0].getDynamicObject()->setProperty ("end_sample", 48000);
    auto second = first.clone();
    second.getDynamicObject()->setProperty ("check_id", "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    auto cue = second["candidates"][0]["cues"][0];
    cue.getDynamicObject()->setProperty ("start_sample", 48000); cue.getDynamicObject()->setProperty ("end_sample", 96000);
    cue.getDynamicObject()->setProperty ("cue_id", "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    second["candidates"][0].getDynamicObject()->setProperty ("default_cue_id", cue["cue_id"]);
    preset["checks"].getArray()->add (second);
    require (writeJson (root.getChildFile ("library/manifest.json"), libraryManifest (root, preset, 1)), "publish two Checks of one source");
    ref::ReferenceComparisonController c (root);
    control::stopWorkers (c); c.configure ({"check-switch-post", {}, 42, true}, 48000, 2);
    c.setPresented (true); control::service (c);
    c.observeTransport (0, true, true); control::service (c);
    const auto state = c.snapshot();
    require (state.checkReady && state.checkTargets.size() >= 2, "two playable Checks are available");
    juce::AudioBuffer<float> block (2, 512);
    int cursor = 0;
    const auto feed = [&] (bool prepare = true)
    {
        for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (block.getWritePointer (ch), 0.125f, 512);
        beginReferenceRtProbe();
        c.observeTransport (cursor, true, true); c.observeAInput (block, cursor, true, true, true);
        const bool rendered = c.renderSelectedB (block, cursor, true, true, true);
        require (endReferenceRtProbe() == 0, "Check switches allocate nothing on the callback");
        for (int ch = 0; ch < 2; ++ch) for (int f = 0; f < 512; ++f)
            require (block.getSample (ch, f) >= 0.125f && block.getSample (ch, f) <= 0.250001f,
                     "C preparation and fades preserve audible A, never insert digital silence");
        cursor += 512; if (prepare) control::service (c); c.servicePendingAudition (-22, -6, true);
        return rendered;
    };
    feed(); require (c.requestAudition (2, -22, -6), "start C explicitly");
    require (feed(), "positive control: C really renders before switching");
    const auto next = state.checkTargets[1].id;
    require (c.selectCheck (next), "change the Check while C is playing");
    for (int n = 0; n < 32; ++n)
    {
        const auto rendered = feed (false);
        if (n > 0) require (! rendered, "after the existing short return fade, withheld publication stays audible A");
    }
    feed();
    require (c.snapshot().checkReady, "one real preparation turn makes the already-loaded Cue available");
    require (feed() && c.snapshot().audibleComparisonSlot == 2
        && c.snapshot().checkSelection->checkId == second["check_id"].toString(),
             "the prepared Cue resumes on the first following callback without an extra waiting interval");
    c.selectA();
}
