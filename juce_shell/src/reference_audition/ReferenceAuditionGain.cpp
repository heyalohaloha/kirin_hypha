#include "ReferenceAuditionController.h"

#include <cmath>

namespace hypha::reference_audition
{
    namespace
    {
        float gainFromDecibels (double gainDb)
        {
            return static_cast<float> (std::pow (10.0, gainDb / 20.0));
        }
    }

    bool Controller::prepareReferenceGain (double aIntegratedLoudness,
                                           double aMaximumTruePeakDbtp) noexcept
    {
        if (! ready.load (std::memory_order_acquire)
            || ! latestPositionValid.load (std::memory_order_acquire)
            || ! std::isfinite (aIntegratedLoudness)
            || ! std::isfinite (aMaximumTruePeakDbtp))
            return false;
        Preparation preparation;
        SourceReceipt receipt;
        {
            const juce::ScopedLock lock (snapshotLock);
            preparation = activePreparation;
            receipt = activeReceipt;
        }
        if (! preparation.valid() || ! receipt.valid())
            return false;
        const double requiredGain = aIntegratedLoudness - receipt.integratedLoudness;
        const bool insufficientHeadroom = requiredGain > 0.0
            && preparation.maxSafePositiveGainDb + 1.0e-9 < requiredGain;
        const double appliedGain = insufficientHeadroom
            ? 0.0
            : juce::jmax (-100.0, requiredGain);
        if (receipt.maximumTruePeakDbtp + appliedGain > -1.0 + 1.0e-9)
            return false;
        const auto hostPosition = latestHostPosition.load (std::memory_order_acquire);
        bHostAnchor.store (hostPosition, std::memory_order_release);
        bLinearGain.store (gainFromDecibels (appliedGain), std::memory_order_release);
        const auto sourcePosition = mappedSourcePosition (hostPosition);
        pages.request (sourcePosition);
        if (! pages.readyAt (sourcePosition, 1))
            return false;
        {
            const juce::ScopedLock lock (snapshotLock);
            currentSnapshot.appliedGainDb = appliedGain;
            currentSnapshot.gainLimited = insufficientHeadroom;
            currentSnapshot.comparisonFallbackOriginal = insufficientHeadroom;
            currentSnapshot.aIntegratedLoudness = aIntegratedLoudness;
            currentSnapshot.aMaximumTruePeakDbtp = aMaximumTruePeakDbtp;
            currentSnapshot.adjustedBIntegratedLoudness =
                receipt.integratedLoudness + appliedGain;
            currentSnapshot.adjustedBMaximumTruePeakDbtp =
                receipt.maximumTruePeakDbtp + appliedGain;
            currentSnapshot.loudnessDeltaBMinusA =
                currentSnapshot.adjustedBIntegratedLoudness - aIntegratedLoudness;
            currentSnapshot.truePeakDeltaBMinusA =
                currentSnapshot.adjustedBMaximumTruePeakDbtp - aMaximumTruePeakDbtp;
        }
        return true;
    }

}
