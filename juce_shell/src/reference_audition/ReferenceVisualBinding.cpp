#include "ReferenceRuntimeV2Controller.h"
#include <cmath>
namespace hypha::reference_audition
{
VisualBinding RuntimeV2Controller::visualBinding() const
{
    const juce::ScopedLock lock (stateLock);
    VisualBinding result;
    const auto calibration = blind.snapshot();
    result.hidden = calibration.phase != BlindPhase::inactive;
    // A transport-only revoke stops output, not the verified display/queued source identity.
    // Invalid/replaced publications clear these pointers; aligned still requires audio readiness.
    const bool validPublication = currentSnapshot.state == RuntimeState::ready
        || currentSnapshot.state == RuntimeState::waiting;
    result.source = validPublication ? publishedSource : nullptr;
    result.overview = result.source ? currentSnapshot.detailedMeasurement : nullptr;
    result.presetId = currentSnapshot.presetId;
    result.checkId = currentSnapshot.checkId;
    if (workspace)
        for (const auto& preset : workspace->presets)
            if (preset.sourcePresetArtifact.presetId == result.presetId)
            {
                result.presetRevisionId = preset.sourcePresetArtifact.revisionId;
                break;
            }
    result.hostRate = static_cast<std::int64_t> (std::llround (requestedConfiguration.sampleRate));
    result.channels = requestedConfiguration.channels;
    result.sourceCueStartSample = visualSourceCueStart;
    result.sourceCueEndSample = visualSourceCueEnd;
    result.matchWindowBlocks = trackingEnabled.load (std::memory_order_acquire) ? liveWindowBlocks : currentSnapshot.cueWindowBlocks;
    const auto generation = mappingGeneration.load (std::memory_order_acquire);
    result.hostAnchor = bHostAnchor.load (std::memory_order_relaxed);
    result.sourceAnchor = bSourceAnchor.load (std::memory_order_relaxed);
    result.aligned = result.source && ready.load (std::memory_order_acquire) && calibration.wholeSong && calibration.eligible
        && generation % 2 == 0 && generation == mappingGeneration.load (std::memory_order_acquire);
    result.hostPositionValid = latestPositionValid.load (std::memory_order_acquire);
    result.hostPosition = result.hostPositionValid
        ? latestHostPosition.load (std::memory_order_acquire) : -1;
    if (! versionComparison && bSelected.load (std::memory_order_acquire) && result.hostPositionValid && result.hostRate > 0)
        if (const auto position = mappedSourcePosition (result.hostPosition); position >= 0)
            result.cuePlayheadSeconds = static_cast<double> (position) / static_cast<double> (result.hostRate);
    if (result.source)
        result.key = result.source->sourceFileSha256 + ":" + juce::String (requestedConfiguration.generation)
            + ":" + juce::String (generation) + ":" + juce::String (calibration.pairedLoudnessDeltaDb, 9)
            + ":cue:" + juce::String (result.sourceCueStartSample) + ":" + juce::String (result.sourceCueEndSample);
    result.key += ":selection:" + result.presetId + ":" + result.presetRevisionId + ":" + result.checkId;
    if (result.aligned)
    {
        result.gainDb = calibration.pairedLoudnessDeltaDb;
        result.matched = std::isfinite (result.gainDb);
        if (result.gainDb > 0.0)
        {
            const auto summary = result.source ? result.source->measurementSummary : std::nullopt;
            const auto peak = summary ? summary->maximumTruePeakDbtp : std::nullopt;
            const auto ceiling = peak ? juce::jmax (-1.0, calibration.aCueTruePeakDbtp, *peak) : -1.0;
            result.matched = peak && ceiling - *peak + 1.0e-9 >= result.gainDb;
        }
        if (!result.matched) result.gainDb = 0.0;
    }
    // While output is selected, display the gain actually installed on that path.
    if (bSelected.load (std::memory_order_acquire) || normalReturnToken.load (std::memory_order_acquire))
    {
        result.gainDb = currentSnapshot.appliedGainDb;
        result.matched = !currentSnapshot.comparisonFallbackOriginal && !currentSnapshot.gainLimited
            && std::isfinite (result.gainDb);
        if (!std::isfinite (result.gainDb)) result.gainDb = 0.0;
    }
    // gain と合わせたかは見せ方（描くときに足す）。何を測るかの鍵（key）には入れない：入れると追従で gain が動くたびに
    // 比べた窓を作り直し、V の画面の WHOLE の線と組の窓が消える。
    return result;
}
}
