#pragma once
#include "ReferenceVersionFormatFixture.h"
#include "ReferenceManualService.h"
#include "reference_rt_probe.h"
#include <limits>

enum class CheckRepress { none, direct, measured, unavailable };

inline void testReferencePreparedCheckSwitch (const juce::File& sandbox, int channels,
                                              bool differentSource, bool rejectGain = false,
                                              bool cancel = false, bool invalidateSource = false,
                                              CheckRepress repress = CheckRepress::none)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile ("check-switch-" + juce::String (channels)
        + (differentSource ? "-file" : "-cue") + (rejectGain ? "-ceiling" : "") + (cancel ? "-cancel" : "")
        + (invalidateSource ? "-changed" : "") + "-" + juce::String (static_cast<int> (repress)));
    require (root.createDirectory().wasOk(), "isolated Check switch fixture");
    const auto makeCandidate = [&] (const juce::String& name, bool alternate)
    {
        const auto file = root.getChildFile (name);
        juce::AudioBuffer<float> pcm (channels, 96000);
        std::vector<float> interleaved (static_cast<size_t> (96000 * channels));
        for (int f = 0; f < 96000; ++f)
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto sample = alternate || f >= 48000 ? 0.5f : 0.25f;
                pcm.setSample (ch, f, sample); interleaved[static_cast<size_t> (f * channels + ch)] = sample;
            }
        {
            auto output = file.createOutputStream(); juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
                output.release(), 48000, static_cast<unsigned> (channels), 32, {}, 0));
            require (writer && writer->writeFromAudioSampleBuffer (pcm, 0, pcm.getNumSamples()), "write audible C fixture");
        }
        const auto hash = juce::SHA256 (file).toHexString(), pcmHash = ref::referenceProbePcmHash (interleaved);
        auto source = makeRuntimeV2Source (file, hash, pcmHash);
        source["audio"].getDynamicObject()->setProperty ("channels", channels);
        addRuntimeV2MeasurementSummary (source, -22, -6);
        return bindRuntimeV2PresetToSource ("88888888-8888-4888-8888-888888888888",
            "99999999-9999-4999-8999-999999999999", stageWholeSongArtifact (root, "sources", source), hash, pcmHash);
    };
    auto preset = makeCandidate ("source.wav", false);
    auto* object = preset.getDynamicObject(); object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0"); object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    auto first = preset["checks"][0];
    first["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    first["candidates"][0]["cues"][0].getDynamicObject()->setProperty ("end_sample", 48000);
    auto second = first.clone();
    second.getDynamicObject()->setProperty ("check_id", "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    if (differentSource)
    {
        const auto other = makeCandidate ("alternate.wav", true)["checks"][0]["candidates"][0];
        for (const auto* key : {"source_identity", "source_artifact"})
            second["candidates"][0].getDynamicObject()->setProperty (key, other[key]);
    }
    auto cue = second["candidates"][0]["cues"][0];
    cue.getDynamicObject()->setProperty ("start_sample", 48000); cue.getDynamicObject()->setProperty ("end_sample", 96000);
    cue.getDynamicObject()->setProperty ("cue_id", "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    second["candidates"][0].getDynamicObject()->setProperty ("default_cue_id", cue["cue_id"]);
    preset["checks"].getArray()->add (second);
    require (writeJson (root.getChildFile ("library/manifest.json"), libraryManifest (root, preset, 1)), "publish two Checks");
    std::atomic<int> gateBalance {0};
    std::atomic<bool> overlappingAdmission {false};
    ref::ReferenceComparisonController c (root, [&] (bool active) {
        if (active ? gateBalance.fetch_add (1) != 0 : gateBalance.fetch_sub (1) != 1)
            overlappingAdmission.store (true);
        return true;
    });
    control::stopWorkers (c); c.configure ({"check-switch-post", {}, 42, true}, 48000, channels);
    c.setPresented (true); control::service (c);
    c.observeTransport (0, true, true); control::service (c);
    const auto state = c.snapshot();
    require (state.checkReady && state.checkTargets.size() >= 2, "two playable Checks are available");
    juce::AudioBuffer<float> block (channels, 512);
    int cursor = 0;
    bool levelAvailable = true;
    const auto feed = [&] (bool prepare = true)
    {
        for (int ch = 0; ch < channels; ++ch) juce::FloatVectorOperations::fill (block.getWritePointer (ch), 0.125f, 512);
        beginReferenceRtProbe();
        c.observeTransport (cursor, true, true); c.observeAInput (block, cursor, true, true, true);
        const bool rendered = c.renderSelectedB (block, cursor, true, true, true);
        require (endReferenceRtProbe() == 0, "Check switches allocate nothing on the callback");
        for (int ch = 0; ch < channels; ++ch) for (int f = 0; f < 512; ++f)
            require (block.getSample (ch, f) >= 0.125f && block.getSample (ch, f) <= 0.500001f,
                     "preparation and fades preserve audible A and bounded reference PCM");
        cursor += 512; if (prepare) control::service (c);
        c.servicePendingAudition (levelAvailable ? (rejectGain ? -2 : -25)
            : std::numeric_limits<double>::quiet_NaN(), -6, true);
        return rendered;
    };
    feed(); require (c.requestAudition (2, -22, -6), "start C explicitly");
    require (feed(), "positive control: C really renders before switching");
    require (c.selectCheck (state.checkTargets[1].id), "change the Check while C is playing");
    // Withhold preparation itself, not a number of milliseconds. Arbitrarily many
    // audio callbacks must keep the old approved C, even when live A is different.
    for (int n = 0; n < 128; ++n)
    {
        require (feed (false), "C keeps playing while its next selection is not prepared");
        for (int ch = 0; ch < channels; ++ch) for (int f = 0; f < 512; ++f)
            require (block.getSample (ch, f) == 0.25f, "no preparation-length return to A or unapproved new C");
    }
    levelAvailable = false;
    control::service (c); // Metadata and PCM alone must not begin the return.
    require (c.pendingSlot() == 2 && c.pendingAuditionNeedsLevel() && c.pendingLiveWindowBlocks() > 0,
             "the processor requests the next Cue's A window while old C still plays");
    for (int n = 0; n < 16; ++n)
    {
        require (feed(), "C also keeps playing while the next MATCH waits for A's window");
        require (block.getSample (0, 511) == 0.25f, "a prepared page never bypasses level readiness");
    }
    if (repress != CheckRepress::none)
    {
        auto& runtime = control::roleFor (c, {2});
        const auto generation = runtime.requestedGeneration();
        require (runtime.awaitingPreparedLevel(), "the next C is waiting for measured A before the repeated press");
        const auto loudness = repress == CheckRepress::unavailable ? std::numeric_limits<double>::quiet_NaN() : -25.0;
        require (repress == CheckRepress::direct ? c.selectC (loudness, -6) : c.requestAudition (2, loudness, -6),
                 "pressing C again accepts the existing selection switch");
        require (runtime.requestedGeneration() == generation,
                 "pressing C again preserves the requested selection generation");
        require (feed() && block.getSample (0, 511) == 0.25f,
                 "pressing C again keeps the old approved PCM while the next A window is unavailable");
    }
    if (invalidateSource)
    {
        require (differentSource, "revision mutation targets only the next source");
        const auto file = root.getChildFile ("alternate.wav");
        auto stream = file.createOutputStream();
        require (stream && stream->setPosition (file.getSize()) && stream->writeByte (0), "change the prepared fixture file");
        stream->flush(); require (stream->getStatus().wasOk(), "flush the changed fixture revision");
    }
    if (cancel) c.selectA();
    levelAvailable = true;
    c.servicePendingAudition (rejectGain ? -2 : -25, -6, true);
    if (repress != CheckRepress::none)
        require (!control::roleFor (c, {2}).awaitingPreparedLevel(),
                 "pressing C again preserves delivery of the next C's measured A window");
    control::service (c);
    feed(); // The bounded return fade, followed by publication and the new MATCH.
    const bool rendered = feed();
    if (cancel || rejectGain || invalidateSource)
    {
        require (!rendered && c.snapshot().audibleComparisonSlot == 0, "cancelled or unsafe new C never acquires output");
        for (int ch = 0; ch < channels; ++ch) for (int f = 0; f < 512; ++f)
            require (block.getSample (ch, f) == 0.125f, "refused new gain preserves live A bit for bit");
        if (rejectGain) require (c.snapshot().pendingAudition.stage == ref::PendingAuditionView::Stage::ceilingExceeded,
                                 "a new Cue requires its own safe MATCH");
        if (invalidateSource) require (c.snapshot().checkSelection->rejectionCode == "reference_source_changed",
                                      "a decoded bank never outlives verification of its source revision");
    }
    else
    {
        const auto after = c.snapshot();
        require (rendered && after.audibleComparisonSlot == 2 && after.checkSelection->checkId == second["check_id"].toString(),
                 "prepared C takes output on the first callback after its return and MATCH");
        require (std::abs (after.checkSelection->appliedGainDb + 3.0) < 1.0e-9, "new C has its own newly matched gain");
        constexpr float expected = 0.3539729f; // 0.5 full-scale PCM at -3 dB.
        require (std::abs (block.getSample (0, 511) - expected) < 1.0e-6f, "the new bank, Cue and gain reach actual PCM output");
    }
    c.selectA();
    feed();
    require (!overlappingAdmission.load() && gateBalance.load() == 0,
             "preparation and replacement preserve one exclusive output admission and retire it on A");
}

inline void testReferenceCheckSwitch (const juce::File& sandbox)
{
    for (auto repress : {CheckRepress::measured, CheckRepress::direct, CheckRepress::unavailable})
        for (int channels : {1, 2})
            for (bool otherFile : {false, true})
                testReferencePreparedCheckSwitch (sandbox, channels, otherFile, false, false, false, repress);
    for (int channels : {1, 2})
        for (bool otherFile : {false, true}) testReferencePreparedCheckSwitch (sandbox, channels, otherFile);
    testReferencePreparedCheckSwitch (sandbox, 2, true, true);
    testReferencePreparedCheckSwitch (sandbox, 2, false, false, true);
    testReferencePreparedCheckSwitch (sandbox, 2, true, false, false, true);
    std::cout << "Reference Check switch: repeated C, prepared PCM, matched gain and safety PASS\n";
}
