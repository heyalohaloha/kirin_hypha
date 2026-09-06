#include "ReferenceRuntimeV2Controller.h"

#include <cmath>
#include <limits>

namespace hypha::reference_audition
{
    namespace
    {
        float gainFromDecibels (double gainDb)
        {
            return static_cast<float> (std::pow (10.0, gainDb / 20.0));
        }

        double unavailable() noexcept
        {
            return std::numeric_limits<double>::quiet_NaN();
        }
    }

    bool RuntimeV2Controller::prepareReferenceGain (
        double aIntegratedLoudness,
        double aMaximumTruePeakDbtp,
        std::uint64_t selectionGeneration) noexcept
    {
        if (! ready.load (std::memory_order_acquire)
            || ! latestPositionValid.load (std::memory_order_acquire)
            || normalSelectionGeneration.load (std::memory_order_acquire)
                != selectionGeneration)
            return false;
        juce::String comparisonMode;
        std::shared_ptr<const RuntimeSource> source;
        std::uint64_t epoch = 0;
        {
            const juce::ScopedLock lock (stateLock);
            if (! ready.load (std::memory_order_acquire)
                || normalSelectionGeneration.load (std::memory_order_acquire)
                    != selectionGeneration)
                return false;
            epoch = auditionEpoch.load (std::memory_order_acquire);
            comparisonMode = currentSnapshot.comparisonMode;
            source = publishedSource;
        }
        if (source == nullptr)
            return false;

        double requiredGain = 0.0;
        bool fallbackOriginal = false;
        if (comparisonMode == "loudness_match")
        {
            if (! std::isfinite (aIntegratedLoudness)
                || ! source->measurementSummary
                || ! source->measurementSummary->loudnessLufsI)
                fallbackOriginal = true;
            else
                requiredGain = aIntegratedLoudness
                             - *source->measurementSummary->loudnessLufsI;
        }
        else if (comparisonMode == "peak_match")
        {
            if (! std::isfinite (aMaximumTruePeakDbtp)
                || ! source->measurementSummary
                || ! source->measurementSummary->maximumTruePeakDbtp)
                fallbackOriginal = true;
            else
                requiredGain = aMaximumTruePeakDbtp
                             - *source->measurementSummary->maximumTruePeakDbtp;
        }
        else if (comparisonMode != "original")
            return false;
        if (! std::isfinite (requiredGain)
            || requiredGain < -100.0 || requiredGain > 100.0)
            return false;

        double appliedGain = requiredGain;
        bool limited = false;
        if (requiredGain > 0.0)
        {
            if (! source->measurementSummary
                || ! source->measurementSummary->maximumTruePeakDbtp)
            {
                appliedGain = 0.0;
                fallbackOriginal = true;
            }
            else
            {
                const auto ceiling = juce::jmax (
                    -1.0,
                    std::isfinite (aMaximumTruePeakDbtp)
                        ? aMaximumTruePeakDbtp : -1.0,
                    *source->measurementSummary->maximumTruePeakDbtp);
                const auto availableGain = juce::jmax (
                    0.0,
                    ceiling - *source->measurementSummary->maximumTruePeakDbtp);
                if (availableGain + 1.0e-9 < requiredGain)
                {
                    appliedGain = 0.0;
                    fallbackOriginal = true;
                }
            }
            limited = appliedGain + 1.0e-9 < requiredGain;
        }

        const auto sourceLoudness = source->measurementSummary
            && source->measurementSummary->loudnessLufsI
            ? *source->measurementSummary->loudnessLufsI : unavailable();
        const auto sourcePeak = source->measurementSummary
            && source->measurementSummary->maximumTruePeakDbtp
            ? *source->measurementSummary->maximumTruePeakDbtp : unavailable();
        PreparedNormalSelection prepared;
        prepared.auditionEpoch = epoch;
        prepared.selectionGeneration = selectionGeneration;
        prepared.linearGain = gainFromDecibels (appliedGain);
        prepared.appliedGainDb = appliedGain;
        prepared.aIntegratedLoudness = aIntegratedLoudness;
        prepared.aMaximumTruePeakDbtp = aMaximumTruePeakDbtp;
        prepared.adjustedBIntegratedLoudness = std::isfinite (sourceLoudness)
            ? sourceLoudness + appliedGain : unavailable();
        prepared.adjustedBMaximumTruePeakDbtp = std::isfinite (sourcePeak)
            ? sourcePeak + appliedGain : unavailable();
        prepared.loudnessDeltaBMinusA = std::isfinite (aIntegratedLoudness)
            && std::isfinite (prepared.adjustedBIntegratedLoudness)
            ? prepared.adjustedBIntegratedLoudness - aIntegratedLoudness : unavailable();
        prepared.truePeakDeltaBMinusA = std::isfinite (aMaximumTruePeakDbtp)
            && std::isfinite (prepared.adjustedBMaximumTruePeakDbtp)
            ? prepared.adjustedBMaximumTruePeakDbtp - aMaximumTruePeakDbtp : unavailable();
        prepared.gainLimited = limited;
        prepared.comparisonFallbackOriginal = fallbackOriginal;
        prepared.valid = true;

        const juce::ScopedLock lock (stateLock);
        if (! ready.load (std::memory_order_acquire)
            || auditionEpoch.load (std::memory_order_acquire) != epoch
            || normalSelectionGeneration.load (std::memory_order_acquire)
                != selectionGeneration)
            return false;
        preparedNormalSelection = prepared;
        return true;
    }

    bool RuntimeV2Controller::activatePreparedB (
        std::uint64_t selectionGeneration) noexcept
    {
        std::uint64_t epoch = 0;
        {
            const juce::ScopedLock lock (stateLock);
            if (! preparedNormalSelection.valid
                || preparedNormalSelection.selectionGeneration != selectionGeneration)
                return false;
            epoch = preparedNormalSelection.auditionEpoch;
        }
        if (! ready.load (std::memory_order_acquire)
            || auditionEpoch.load (std::memory_order_acquire) != epoch
            || normalSelectionGeneration.load (std::memory_order_acquire)
                != selectionGeneration
            || ! latestPlaying.load (std::memory_order_acquire)
            || ! latestPositionValid.load (std::memory_order_acquire))
            return false;
        const auto sourcePosition = mappedSourcePosition (latestHostPosition.load());
        pages.request (sourcePosition);
        if (! pages.readyAt (sourcePosition, 1))
            return false;
        const auto gateToken = acquireOutputGate();
        if (gateToken == 0)
            return false;

        bool publicationStillValid = false;
        {
            const juce::ScopedLock lock (stateLock);
            publicationStillValid = ready.load (std::memory_order_acquire)
                && auditionEpoch.load (std::memory_order_acquire) == epoch
                && normalSelectionGeneration.load (std::memory_order_acquire)
                    == selectionGeneration
                && preparedNormalSelection.valid
                && preparedNormalSelection.auditionEpoch == epoch
                && preparedNormalSelection.selectionGeneration == selectionGeneration;
            if (! publicationStillValid)
                preparedNormalSelection.valid = false;
            else
            {
                const auto& prepared = preparedNormalSelection;
                bLinearGain.store (prepared.linearGain, std::memory_order_release);
                currentSnapshot.appliedGainDb = prepared.appliedGainDb;
                currentSnapshot.aIntegratedLoudness = prepared.aIntegratedLoudness;
                currentSnapshot.aMaximumTruePeakDbtp = prepared.aMaximumTruePeakDbtp;
                currentSnapshot.adjustedBIntegratedLoudness =
                    prepared.adjustedBIntegratedLoudness;
                currentSnapshot.adjustedBMaximumTruePeakDbtp =
                    prepared.adjustedBMaximumTruePeakDbtp;
                currentSnapshot.loudnessDeltaBMinusA = prepared.loudnessDeltaBMinusA;
                currentSnapshot.truePeakDeltaBMinusA = prepared.truePeakDeltaBMinusA;
                currentSnapshot.gainLimited = prepared.gainLimited;
                currentSnapshot.comparisonFallbackOriginal =
                    prepared.comparisonFallbackOriginal;
                preparedNormalSelection.valid = false;
            }
        }
        if (! publicationStillValid)
        {
            releaseOutputGate (gateToken);
            return false;
        }
        activeAuditionEpoch.store (epoch, std::memory_order_release);
        bSelected.store (true, std::memory_order_release);
        if (! ready.load (std::memory_order_acquire)
            || auditionEpoch.load (std::memory_order_acquire) != epoch
            || normalSelectionGeneration.load (std::memory_order_acquire)
                != selectionGeneration)
        {
            bSelected.store (false, std::memory_order_release);
            activeAuditionEpoch.store (0, std::memory_order_release);
            releaseOutputGate (gateToken);
            return false;
        }
        return true;
    }

    bool RuntimeV2Controller::selectB (double aIntegratedLoudness,
                                       double aMaximumTruePeakDbtp) noexcept
    {
        if (blind.ongoing())
            return false;
        const auto generation = normalSelectionGeneration.fetch_add (
            1, std::memory_order_acq_rel) + 1;
        const bool alreadySelected = bSelected.load (std::memory_order_acquire);
        const auto bBaseline = bAudibleConfirmations.load (std::memory_order_acquire);
        const bool selected = prepareReferenceGain (
            aIntegratedLoudness, aMaximumTruePeakDbtp, generation)
                           && activatePreparedB (generation);
        if (selected && ! alreadySelected)
            beginAuditionEventSession (bBaseline);
        return selected;
    }

    void RuntimeV2Controller::selectA() noexcept
    {
        normalSelectionGeneration.fetch_add (1, std::memory_order_acq_rel);
        if (blind.ongoing())
        {
            endBlind();
            return;
        }
        const auto aBaseline = aAudibleConfirmations.load (std::memory_order_acquire);
        const bool wasSelected = bSelected.exchange (false, std::memory_order_acq_rel);
        activeAuditionEpoch.store (0, std::memory_order_release);
        const bool deferredReturn = auditionReturnPending.exchange (
            false, std::memory_order_acq_rel);
        const auto deferredRelease = gateReleasePendingToken.exchange (
            0, std::memory_order_acq_rel);
        if (wasSelected || deferredReturn)
            requestAuditionReturnEvent (aBaseline);
        if (wasSelected)
            releaseActiveOutputGate();
        else
            releaseOutputGate (deferredRelease);
    }
}
