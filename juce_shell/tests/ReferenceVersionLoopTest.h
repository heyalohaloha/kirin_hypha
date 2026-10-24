#pragma once
#include "ReferenceVersionFormatFixture.h"
#include "ReferenceManualService.h"
#include "ReferencePageWaitTestAccess.h"
#include "reference_rt_probe.h"

inline void testReferenceVersionLoop (const juce::File& sandbox, bool workingMix = false, bool freshCapture = false, bool mediaSplice = false)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile (mediaSplice ? "version-splice-proof" : freshCapture ? "version-fresh-proof" : workingMix ? "version-mix-loop" : "version-copy-loop");
    require (root.createDirectory().wasOk(), "isolated short-loop fixture");
    auto base = makeWholeSongFixture (root, root.getChildFile ("input.wav"),
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333");
    base.audio.clear (48000, 512); // Silence alone cannot confirm a newly wrapped map.
    const auto manifest = hypha::tests::version_formats::publish (root, base.audio, 48000, false, 0, 1);
    require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "publish the measured loop Version");
    const auto entry = manifest["versions"][0]["entry_id"].toString();
    const auto id = entry + "/" + entry + "/" + entry;
    ref::ReferenceComparisonController c (root);
    control::stopWorkers (c); c.configure ({"loop-post", {}, 42, true}, 48000, 2);
    control::service (c); require (c.selectVersion (id), "choose the loop Version");
    c.setPresented (true); control::service (c);
    juce::AudioBuffer<float> input (2, 256), original (2, 256);
    double approvedGain = 0.0;
    const auto feed = [&] (int source, int host, bool render, bool serviceWorkspace = true)
    {
        for (int ch = 0; ch < 2; ++ch) for (int f = 0; f < 256; ++f)
        {
            const auto sample = base.audio.getSample (ch, source + f);
            input.setSample (ch, f, workingMix ? sample + 0.008f * std::tanh (sample * 18.0f) : sample);
        }
        original.makeCopyOf (input, true);
        beginReferenceRtProbe();
        c.observeTransport (host, true, true); c.observeAInput (input, host, true, true, true);
        const bool sounded = render && c.renderSelectedB (input, host, true, true, true);
        require (endReferenceRtProbe() == 0, "loop recovery allocates nothing on the audio callback");
        if (sounded)
        {
            const auto gain = static_cast<float> (std::pow (10.0, approvedGain / 20.0));
            for (int ch = 0; ch < 2; ++ch) for (int f = 240; f < 256; ++f)
                require (std::abs (input.getSample (ch, f) - base.audio.getSample (ch, source + f) * 0.8f * gain) < 2.0e-6f,
                         "the selected Version plays the current loop position with its approved gain");
        }
        else
            for (int ch = 0; ch < 2; ++ch)
                require (std::memcmp (input.getReadPointer (ch), original.getReadPointer (ch), 256 * sizeof (float)) == 0,
                         "unconfirmed loop blocks preserve live A bit for bit");
        if (serviceWorkspace) { control::service (c); c.servicePendingAudition (-22, -6, true); }
        return sounded;
    };
    for (int at = 0; at < 192000; at += 256)
    {
        feed (at, at, false);
        if (at == 96000 - 256)
            require (! c.snapshot().versionReady, "initial alignment still needs a full capture, not a retained candidate");
    }
    const auto initial = c.snapshot();
    if (! initial.versionReady && initial.versionSelection)
        std::cerr << "Initial Version preparation: " << initial.versionSelection->rejectionCode << '\n';
    require (initial.versionReady, "initial full alignment is ready");
    require (c.requestAudition (1, -22, -6), "initial full alignment starts V");
    approvedGain = c.snapshot().versionSelection->appliedGainDb;
    require (std::abs (approvedGain) > 0.5, "positive control uses an audible non-unity MATCH gain");
    require (feed (192000, 192000, true), "positive control hears V before entering a short loop");
    auto& runtime = control::roleFor (c, {1});
    auto& pages = control::currentPagesFor (runtime, {});
    control::stopPageRefill (pages);
    const auto beforeWait = control::publicationFactsFor (runtime, {});
    const auto selectionTicket = runtime.normalSelectionTicket();
    require (control::retirePagesFor (pages, {}) && !pages.readyAt (192256, 256),
             "page starvation is established before the audio callback");
    require (!feed (192256, 192256, true, false), "unavailable V pages return bit-identical A");
    const auto waiting = control::publicationFactsFor (runtime, {});
    require (waiting.ready && waiting.epoch == beforeWait.epoch && runtime.outputSelected()
        && runtime.normalSelectionTicket() == selectionTicket && !waiting.snapshot.auditionBuffered,
             "page wait keeps the preparation, explicit selection and gain permission while showing loading");
    control::servicePages (pages, {});
    require (pages.readyAt (192512, 256), "only the requested audio pages are supplied");
    require (feed (192512, 192512, true, false),
             "the first page-ready callback resumes the same V without workspace service");
    require (runtime.normalSelectionTicket() == selectionTicket
        && c.snapshot().versionSelection
        && std::abs (c.snapshot().versionSelection->appliedGainDb - approvedGain) < 1.0e-9,
             "page recovery keeps the exact approved non-unity gain");
    if (mediaSplice)
    {
        bool resumed = false;
        // Keep invoking the real render path while A supplies enough continuous
        // content for a new offset, including the one-second capture grid margin.
        for (int source = 0; source < 288000; source += 256)
        {
            const bool sounded = feed (source, 192256 + source, true);
            if (source == 0) require (! sounded, "a continuous-clock splice rejects future V PCM immediately");
            resumed = sounded || resumed;
        }
        const auto current = c.snapshot();
        require (resumed && current.selectedVersionId == id && current.versionSelection
            && std::abs (current.versionSelection->appliedGainDb - approvedGain) < 1.0e-9,
                 "repeated candidate rejection still permits a fresh map and the same approved gain");
        c.selectA();
        return;
    }
    if (freshCapture)
    {
        // Retire the discontinuity before the exact passage; no queued first block
        // may be discarded with the preceding capture on a slow consumer.
        c.observeTransport (0, true, true);
        c.observeAInput (input, 0, false, false, true);
        control::service (c);
        const auto& capture = control::captureFor (control::roleFor (c, {1}), {});
        require (! capture.currentAudio() && capture.observationQueueDrained(),
                 "retire the preceding capture before fresh PCM");
        feed (0, 0, true);
        require (c.snapshot().versionSelection && ! c.snapshot().versionSelection->blindEligible,
                 "a matching block never upgrades the old full Blind calibration");
        for (int at = 256; at < 192000; at += 256) feed (at, at, true);
        const auto current = c.snapshot();
        require (current.versionReady && current.versionSelection && current.versionSelection->blindEligible,
                 "new continuous PCM including silence renews the same full calibration after a seek");
        c.selectA();
        return;
    }
    const auto mapKey = control::roleFor (c, {1}).visualBinding().key;
    // Each lap supplies only two seconds: no fresh four-second capture can finish.
    for (int lap = 0; lap < 3; ++lap)
    {
        bool resumed = false;
        for (int at = 48000; at < 144000; at += 256)
        {
            if (at == 48000) require (control::retirePagesFor (pages, {}), "hold page refill across the loop wrap");
            if (at == 48512) control::servicePages (pages, {});
            const bool sounded = feed (at, at, true, at >= 48768);
            if (at == 48512)
                require (sounded, "fresh loop PCM resumes on the first page-ready block before workspace service");
            if (! workingMix && at == 48000)
                require (! sounded && ! control::roleFor (c, {1}).visualBinding().aligned,
                         "silence alone neither resumes V nor renews a stale map proof");
            resumed = sounded || resumed;
        }
        require (resumed, "V resumes within every two-second loop without a new full capture");
        const auto display = control::roleFor (c, {1}).visualBinding();
        require (display.aligned && display.key == mapKey, "fresh loop proof keeps the confirmed display map and history identity");
        require (c.snapshot().selectedVersionId == id && c.snapshot().versionSelection
            && std::abs (c.snapshot().versionSelection->appliedGainDb - approvedGain) < 1.0e-9
            && c.snapshot().pendingAudition.stage != ref::PendingAuditionView::Stage::sourceChanged,
                 "loop wraps retain the selected Version and its approved gain");
    }
    if (! workingMix)
    {
        require (! feed (96000, 48000, true), "a retained map alone cannot authorize mismatched current PCM");
        require (! control::roleFor (c, {1}).visualBinding().aligned, "a rejected candidate never reuses the old display proof");
        bool resumed = false;
        for (int at = 48256; at < 144000; at += 256) resumed = feed (at, at, true) || resumed;
        require (resumed, "fresh matching PCM restores V after a rejected candidate block");
    }
    require (control::retirePagesFor (pages, {}), "a second page wait precedes source withdrawal");
    require (!feed (144000, 144000, true, false), "a missing page still preserves the complete A block");
    const auto source = runtime.visualBinding().source;
    pages.close(); // Release the fixture decoder before removing its file on every platform.
    require (source && juce::File (source->absolutePath).deleteFile(), "remove only the disposable registered source");
    control::service (c);
    const auto withdrawn = runtime.snapshot();
    require (!feed (144256, 144256, true, false) && !runtime.outputSelected()
        && withdrawn.state != ref::RuntimeState::ready && withdrawn.rejectionCode.isNotEmpty(),
             "source loss during a page wait revokes V and exposes the failure reason");
    c.selectA();
}
