#pragma once
#include "HyphaMetricSharedZero.h"
#include "HyphaLuReferenceFacts.h"
#include "HyphaMetricReferenceState.h"
#include "reference_audition/ReferenceMetricPublication.h"
namespace hypha::metric_needle
{
inline SharedReferenceZero referenceZero (const reference_audition::Snapshot& runtime, bool readable,
    double hostRate, std::uint32_t hostChannels, std::size_t i)
{
    using namespace reference_audition;
    if (i >= 3) return {};
    const auto* role = lu_needle::zeroRole (runtime);
    const auto measured = role ? role->nativeMetrics : nullptr;
    metric_needle::SharedReferenceZero zero;
    zero.face = static_cast<metric_needle::Face> (i + 2);
    zero.rate = std::isfinite (hostRate) && hostRate >= 8000 && hostRate <= 384000
        ? static_cast<std::uint32_t> (hostRate) : 0u;
    zero.channels = hostChannels;
    if (readable && role)
    {
        const auto waiting = unmeasured (runtime, *role);
        zero.song = role->candidateName; zero.status = waiting.status; zero.reason = waiting.reason;
    }
    if (readable && role && measured && measured->selectionGeneration == role->selectionGeneration)
    {
        zero.status = metric_needle::Status::unavailable;
        zero.reason = (measured->cueReasons[i].isNotEmpty() ? measured->cueReasons[i] : measured->reason).toStdString();
        zero.generation = measured->selectionGeneration;
        zero.song = role->candidateName;
        if (measured->state == metrics::Availability::measuring)
        { zero.status = metric_needle::Status::measuring; if (zero.reason.empty()) zero.reason = "preparing"; }
        if (measured->series)
        {
            zero.rate = measured->series->rate; zero.channels = measured->series->channels;
            const auto& cue = measured->cue[i];
            if (measured->state == metrics::Availability::ready && cue.complete && cue.observations > 0
                && std::isfinite (cue.median))
            { zero.status = metric_needle::Status::ready; zero.median = cue.median;
                if (measured->setComplete) zero.band = {measured->bands[i].low, measured->bands[i].high}; }
        }
    }
    if (zero.status == metric_needle::Status::unavailable && zero.reason.empty()) zero.reason = "no_valid_value";
    if (!readable || !role) zero.reason = "select_reference_post";
    return zero;
}
}
