#pragma once

#include "ExactRangeCapture.h"
#include "LocalBlindTrial.h"
#include <functional>
#include <memory>

namespace hypha::local_blind
{
enum class GainMatchPolicy : unsigned char { alignedActiveBlocksV1, exactTrackEventEnergyV1 };
enum class PreparationFailure { none, incompleteCapture, rangeMismatch, capacity, gainUnavailable, preparationFailed };
struct PreparedCandidate
{
    std::unique_ptr<LocalBlindTrial> trial;
    PreparationFailure failure = PreparationFailure::none;
    GainMatchPolicy gainPolicy = GainMatchPolicy::alignedActiveBlocksV1;
    double fixedPreGainDb = 0, lowerPostGainDb = 0;
    std::uint64_t matchedAnalysisUnits = 0;
};

// Non-RT preparation only, not a scope/PDC certificate and not an audition start route.
// The owner must independently prove that expectedPreStart maps to playback.start, and that the
// objects belong to the exact selected PRE/POST. No file is forged as a Kirin OS work_version.
PreparedCandidate prepareLocalBlindCandidate (
    const ExactRangeCapture& post, const ExactRangeCapture& pre, const TrialFormat& playback,
    std::int64_t expectedPreStart, std::size_t frozenPcmBudget, GainMatchPolicy,
    const std::function<bool()>& secureAssignmentBit) noexcept;
}
