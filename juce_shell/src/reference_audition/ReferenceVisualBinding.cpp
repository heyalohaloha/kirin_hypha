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
    result.source = ready.load (std::memory_order_acquire) ? publishedSource : nullptr;
    result.overview = result.source ? currentSnapshot.detailedMeasurement : nullptr;
    result.hostRate = static_cast<std::int64_t> (std::llround (requestedConfiguration.sampleRate));
    result.channels = requestedConfiguration.channels;
    const auto generation = mappingGeneration.load (std::memory_order_acquire);
    result.hostAnchor = bHostAnchor.load (std::memory_order_relaxed);
    result.sourceAnchor = bSourceAnchor.load (std::memory_order_relaxed);
    result.aligned = ready.load (std::memory_order_acquire) && calibration.wholeSong && calibration.eligible
        && generation % 2 == 0 && generation == mappingGeneration.load (std::memory_order_acquire);
    result.hostPositionValid = latestPositionValid.load (std::memory_order_acquire);
    result.hostPosition = result.hostPositionValid
        ? latestHostPosition.load (std::memory_order_acquire) : -1;
    if (result.source)
        result.key = result.source->sourceFileSha256 + ":" + juce::String (requestedConfiguration.generation)
            + ":" + juce::String (generation) + ":" + juce::String (calibration.pairedLoudnessDeltaDb, 9);
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
    result.captureEvidence = !result.hidden ? publishedCaptureEvidence : nullptr;
    result.key += ":gain:" + juce::String (result.gainDb, 9);
    return result;
}
}
