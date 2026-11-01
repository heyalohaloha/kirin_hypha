#pragma once
#include "reference_runtime_v2_analysis_test_support.h"
#include "ReferenceManualService.h"
#include "ReferenceWholeSongAudioFixture.h"
#include "reference_library_manifest_fixture.h"
#include "reference_rt_probe.h"
#include "ReferenceVersionFormatFixture.h"

namespace hypha::tests::reference_control
{
// 停止位置を「確かめた写像の上で V の範囲外」に置く試験用の鍵（ReferencePageWaitFailureTest.h と同じ仕組み、別の札）。
struct VersionStopPinTag {};
void pinVersionStopPosition (Runtime&, std::int64_t, std::int64_t, VersionStopPinTag);
std::int64_t mappedVersionPosition (Runtime&, std::int64_t, VersionStopPinTag);
template <auto Lock, auto Generation, auto Host, auto Source, auto Mapped> struct VersionStopPinAccess
{
    friend void pinVersionStopPosition (Runtime& r, std::int64_t host, std::int64_t source, VersionStopPinTag)
    {
        const juce::ScopedLock lock (r.*Lock);
        (r.*Generation).fetch_add (1, std::memory_order_acq_rel);
        (r.*Host).store (host - source, std::memory_order_relaxed);
        (r.*Source).store (0, std::memory_order_relaxed);
        (r.*Generation).fetch_add (1, std::memory_order_release);
    }
    friend std::int64_t mappedVersionPosition (Runtime& r, std::int64_t host, VersionStopPinTag) { return (r.*Mapped) (host); }
};
template struct VersionStopPinAccess<&Runtime::mappingWriteLock, &Runtime::mappingGeneration,
    &Runtime::bHostAnchor, &Runtime::bSourceAnchor, &Runtime::mappedSourcePosition>;
}

inline void testReferenceVersionPosition (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile ("version-position");
    require (root.createDirectory().wasOk(), "position fixture directory");
    const auto file = root.getChildFile ("version.wav");
    const juce::String recording = "22222222-2222-4222-8222-222222222222";
    const juce::String version = "33333333-3333-4333-8333-333333333333";
    const auto fixture = makeWholeSongFixture (root, file, recording, version);
    auto preset = bindRuntimeV2WorkVersionPresetToSource ("88888888-8888-4888-8888-888888888888",
        "99999999-9999-4999-8999-999999999999", fixture.receipt,
        juce::SHA256 (file).toHexString(), fixture.pcmHash, recording, version, fixture.audio.getNumSamples());
    auto* p = preset.getDynamicObject();
    p->setProperty ("format", "kirin_hypha_reference_library_preset");
    p->setProperty ("version", "1.0"); p->removeProperty ("work_id"); p->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    auto manifest = independentLibraryManifest (root, preset, 1);
    const auto alternative = hypha::tests::version_formats::publish (root, fixture.audio, 48000, false, 0, 2);
    manifest["versions"].getArray()->add (alternative["versions"][0]);
    const auto alternativeEntry = alternative["versions"][0]["entry_id"].toString();
    const auto alternativeId = alternativeEntry + "/" + alternativeEntry + "/" + alternativeEntry;
    require (writeJson (root.getChildFile ("library/manifest.json"), manifest),
             "publish a measured Version in disposable storage");
    ref::ReferenceComparisonController c (root);
    control::stopWorkers (c);
    c.configure ({ "position-post", {}, 42, true }, 48000, 2);
    control::service (c);
    require (! c.snapshot().versions.empty(), "Version catalog is published");
    const auto id = c.snapshot().versions.front().id;
    require (c.selectVersion (id, true), "AUTO initially identifies A's registered Version");
    c.setPresented (true);
    control::service (c);
    juce::AudioBuffer<float> input (2, 256), original (2, 256);
    const auto feed = [&] (int source, int host, bool render)
    {
        for (int ch = 0; ch < 2; ++ch) input.copyFrom (ch, 0, fixture.audio, ch, source, 256);
        original.makeCopyOf (input, true);
        if (render) beginReferenceRtProbe();
        c.observeTransport (host, true, true);
        c.observeAInput (input, host, true, true, true);
        bool sounded = false;
        if (render)
        {
            sounded = c.renderSelectedB (input, host, true, true, true);
            require (endReferenceRtProbe() == 0, "position recovery allocates nothing on the callback");
            if (sounded)
                for (int ch = 0; ch < 2; ++ch)
                    require (std::memcmp (input.getReadPointer (ch), original.getReadPointer (ch), 256 * sizeof (float)) == 0,
                             "the identical registered V renders this position, never future PCM");
        }
        control::service (c);
        c.servicePendingAudition (-22, -6, true);
        return sounded;
    };
    const auto calibrate = [&] (int source, int host)
    {
        for (int frame = 0; frame < 192000; frame += 256) feed (source + frame, host + frame, false);
        require (c.snapshot().versionReady, "four seconds of actual PCM establishes the map");
    };
    calibrate (0, 0);
    require (c.requestAudition (1, -22, -6), "V starts with its matched registered PCM");
    require (feed (192000, 192000, true), "positive control: V really renders before relocation");
    const auto chosen = c.snapshot().selectedVersionId;
    const auto gain = c.snapshot().versionSelection->appliedGainDb;
    // A contiguous DAW clock can cross the end of a repeated media item. The old
    // map points four seconds ahead; that block must stay A, not play future V.
    require (! feed (0, 192256, true), "a media splice never plays the old map ahead of A");
    for (int ch = 0; ch < 2; ++ch)
        require (std::memcmp (input.getReadPointer (ch), original.getReadPointer (ch), 256 * sizeof (float)) == 0,
                 "the unconfirmed splice block is bit-identical live A");
    require (! c.selectVersion (alternativeId, true), "AUTO cannot replace a held V during realignment");
    calibrate (256, 192512);
    require (c.snapshot().selectedVersionId == chosen
        && c.snapshot().pendingAudition.stage != ref::PendingAuditionView::Stage::sourceChanged,
        "the same file at a new position never becomes SOURCE CHANGED");
    require (feed (192256, 384512, true), "V automatically resumes at the newly confirmed position");
    require (std::abs (c.snapshot().versionSelection->appliedGainDb - gain) < 1.0e-9, "recovery preserves the approved gain");
    // This seek also changes the host-to-content offset after the media splice.
    // Reject the candidate until fresh content establishes that new offset.
    require (! feed (0, 0, true), "a seek with a changed content offset rejects the old candidate map");
    require (! c.selectVersion (alternativeId, true), "AUTO cannot replace V during a seek");
    calibrate (256, 256);
    require (feed (192256, 192256, true), "the same V resumes after a host seek");
    require (c.snapshot().selectedVersionId == chosen, "seek retains the selected Version");
    // 再生が V の終わりを越えて RT が A に戻っても、同じ V へ戻る自動の戻しは捨てない。範囲外のあいだは A のまま待ち、
    // ループで範囲内に戻れば押し直し無しで V に戻る（1.1.53 と同じ）。この fixture では範囲外の callback の後の写像は
    // mappingUnstable になる（outsideSource は出ない）ので、outsideSource で戻しを捨てない直し
    // （ReferenceComparisonPending.cpp）は読みで確かめ、ここでは待ちと戻りの形を固定する。
    {
        auto& runtime = control::roleFor (c, { 1 });
        require (runtime.snapshot().blindEligible, "the map is proven by content while V plays");
        const int endHost = 192512 + 256 * 4;
        const auto outsidePosition = static_cast<std::int64_t> (fixture.audio.getNumSamples()) + 48000;
        control::pinVersionStopPosition (runtime, endHost, outsidePosition, {});
        require (control::mappedVersionPosition (runtime, endHost, {}) == outsidePosition, "the callback position maps past the end of V");
        require (! feed (endHost, endHost, true), "a callback past the end of V returns A");
        for (int n = 0; n < 4; ++n) { control::service (c); c.servicePendingAudition (-22, -6, true); }
        const auto past = c.snapshot();
        std::cout << "POSITION phase=past-end pages=" << int (runtime.snapshot().auditionPages)
                  << " stage=" << int (past.pendingAudition.stage) << " waiting=" << past.pendingAudition.waiting()
                  << " reason=" << past.pendingAudition.reasonCode << std::endl;
        require (past.pendingAudition.reasonCode != "reference_position_outside_source"
                     && past.pendingAudition.stage != ref::PendingAuditionView::Stage::startFailed
                     && past.pendingAudition.waiting(),
                 "playing past the end of V keeps the automatic return waiting instead of refusing it as a press");
        control::pinVersionStopPosition (runtime, endHost, static_cast<std::int64_t> (endHost), {});
        bool resumed = false;
        for (int at = endHost + 256; at < endHost + 96000 && ! resumed; at += 256) resumed = feed (at, at, true);
        require (resumed && c.snapshot().selectedVersionId == chosen,
                 "the loop returning inside V resumes the same V without another press");
        require (std::abs (c.snapshot().versionSelection->appliedGainDb - gain) < 1.0e-9, "the return keeps the approved gain");
    }
    // Change disposable file bytes without unlinking an open decoder (Windows
    // may deny deletion of an open file). The old verified revision must end.
    {
        auto stream = file.createOutputStream();
        require (stream && stream->setPosition (file.getSize()) && stream->writeByte (0),
                 "change only disposable reference media");
        stream->flush();
        require (stream->getStatus().wasOk(), "the fixture revision is durably changed");
    }
    control::service (c); c.servicePendingAudition (-22, -6, true);
    require (! c.snapshot().versionReady
        && c.snapshot().pendingAudition.stage == ref::PendingAuditionView::Stage::sourceChanged,
        "an actual file revision ends V authority with a source-change reason");
    require (c.snapshot().pendingAudition.reasonCode == "reference_source_changed",
             "the stop retains its verified file-revision reason for the user-facing message");
    require (! feed (192512, 192512, true), "unverified changed media never plays from an old page");
    for (int ch = 0; ch < 2; ++ch)
        require (std::memcmp (input.getReadPointer (ch), original.getReadPointer (ch), 256 * sizeof (float)) == 0,
                 "actual media rejection also preserves live A bit for bit");
}
