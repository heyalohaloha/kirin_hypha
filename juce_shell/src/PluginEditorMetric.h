#pragma once
#include "HyphaMetricNeedle.h"
#include "HyphaLuReferenceFacts.h"
#include "reference_audition/ReferenceMetricPublication.h"
#include "PluginProcessor.h"
#include "HyphaMetricReferenceZero.h"

namespace hypha::metric_editor
{
inline metric_needle::Frame preFacts (KirinHyphaProcessorBase& processor, metric_needle::Face face, bool readable)
{
    metric_needle::Frame result;
    result.face = face; result.ownIsPre = true;
    const auto zero = processor.sharedMetricZero (face, readable);
    result.context.playing = processor.isPlaying();
    // The PRE record exposes a publisher and monotonic publication revision, not a separate
    // selection epoch. Conservatively repaint on each new record, including same-name selections.
    result.context.referenceIdentity = zero.publisher.toStdString();
    result.context.referenceRevision = zero.generation;
    result.status = zero.status; result.median = zero.median; result.band = zero.band;
    result.song = zero.song.toStdString(); result.reason = zero.reason;
    if (result.reason.empty() && result.status == metric_needle::Status::unavailable) result.reason = "select_reference_post";
    if (result.status == metric_needle::Status::ready && result.context.playing && processor.hasLiveInput()
        && (face == metric_needle::Face::psr || face == metric_needle::Face::crest))
    {
        KirinReferenceLiveMetricFrame live {};
        if (processor.pollReferenceLiveMetrics (live) && live.sample_rate == zero.rate && live.channels == zero.channels)
        {
            const bool psr = face == metric_needle::Face::psr;
            if (live.valid_mask & (psr ? 2u : 1u)) result.own = psr ? live.own_psr_db : live.own_crest_db;
        }
    }
    if (result.status == metric_needle::Status::ready && face == metric_needle::Face::sharp
        && result.context.playing && processor.hasLiveInput())
    {
        KirinPreSharpFrame live {};
        if (processor.pollPreSharp (live) && live.sample_rate == zero.rate && live.channels == zero.channels
            && live.aperture_samples == zero.rate / 10) result.own = live.sharpness_acum;
    }
    return result;
}
inline metric_needle::Frame facts (KirinHyphaProcessorBase& processor,
    const reference_audition::Snapshot& runtime, metric_needle::Face face)
{
    using namespace reference_audition;
    metric_needle::Frame result;
    result.face = face;
    result.context.playing = processor.isPlaying();
    result.context.playbackRun = processor.liveComparePlaybackRun();
    const auto* role = lu_needle::zeroRole (runtime);
    if (!metric_needle::metric (face) || !role) return result;
    result.context.referenceRevision = role->selectionGeneration;
    const auto slot = role == runtime.referenceSelection.get() ? "B/" : "C/";
    result.context.referenceIdentity = (juce::String (slot) + role->presetId + "/"
        + role->checkId + "/" + role->candidateId + "/" + role->cueId).toStdString();
    result.song = role->candidateName.toStdString();
    const auto measured = role->nativeMetrics;
    if (!measured || measured->selectionGeneration != role->selectionGeneration)
    { result.status = metric_needle::Status::measuring; result.reason = "preparing"; return result; }
    const auto index = face == metric_needle::Face::psr ? 0u : face == metric_needle::Face::crest ? 1u : 2u;
    result.reason = (measured->cueReasons[index].isNotEmpty() ? measured->cueReasons[index] : measured->reason).toStdString();
    if (measured->state == metrics::Availability::measuring)
    { result.status = metric_needle::Status::measuring; if (result.reason.empty()) result.reason = "preparing"; return result; }
    if (measured->state != metrics::Availability::ready || !measured->series) return result;
    const auto& cue = measured->cue[index];
    if (!cue.complete || cue.observations == 0 || !std::isfinite (cue.median))
    { if (result.reason.empty()) result.reason = "no_valid_value"; return result; }
    result.median = cue.median;
    if (measured->setComplete) result.band = {measured->bands[index].low, measured->bands[index].high};
    result.status = metric_needle::Status::ready;
    const auto& series = *measured->series;
    if (result.context.playing && processor.hasLiveInput())
    {
        if (face == metric_needle::Face::sharp)
        {
            if (processor.pairStatus() != KIRIN_PAIR_STATUS_PAIRED)
            {
                KirinAbsoluteBatch batch {};
                if (processor.pollAbsoluteBatch (batch) && batch.latest.has_data
                    && batch.latest.sample_rate == series.rate && batch.latest.channels == series.channels
                    && batch.latest.aperture_samples == series.rate / 10)
                    result.own = batch.latest.sharpness;
            }
            KirinPerceptualView view {};
            if (processor.pairStatus() == KIRIN_PAIR_STATUS_PAIRED && processor.pollPerceptual (view) && view.has_data && view.channel_mode == KIRIN_SPECTRUM_CHANNEL_LR
                && view.sample_rate == series.rate && view.channels == series.channels
                && view.aperture_samples == series.rate / 10)
            { result.own = view.post_sharpness; result.pre = view.pre_sharpness; }
        }
        else
        {
            KirinReferenceLiveMetricFrame live {};
            if (processor.pollReferenceLiveMetrics (live) && live.sample_rate == series.rate && live.channels == series.channels)
            {
                const bool psr = face == metric_needle::Face::psr;
                if (live.valid_mask & (psr ? 2u : 1u)) result.own = psr ? live.own_psr_db : live.own_crest_db;
                if (live.valid_mask & (psr ? 8u : 4u)) result.pre = psr ? live.pre_psr_db : live.pre_crest_db;
            }
        }
    }
    if (runtime.followA && role->followA && role->transportPositionValid
        && runtime.blindPhase == BlindPhase::inactive && role->blindPhase == BlindPhase::inactive
        && std::isfinite (role->followPositionSeconds) && role->followPositionSeconds >= 0)
    {
        const auto sample = role->followPositionSeconds * static_cast<double> (series.rate);
        if (sample < static_cast<double> (series.sourceFrames))
            result.reference = metrics::atPosition (series.frames, static_cast<metrics::Metric> (index),
                static_cast<std::uint64_t> (sample), series.sourceFrames, series.rate / 10);
    }
    // Set band is supplied only after every member completed the same definition.
    return result;
}
}
