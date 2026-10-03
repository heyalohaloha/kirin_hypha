#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceTrackingGain.h"

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
        std::uint64_t selectionGeneration, const juce::String& expectedPlaybackIdentity) noexcept
    {
        if (! ready.load (std::memory_order_acquire)
            || ! latestPositionValid.load (std::memory_order_acquire)
            || normalSelectionGeneration.load (std::memory_order_acquire)
                != selectionGeneration)
            return false;
        juce::String comparisonMode;
        std::shared_ptr<const RuntimeSource> source;
        std::uint64_t epoch = 0;
        bool cueLevel = false;
        double cueLoudness = unavailable(), cuePeak = unavailable();
        {
            const juce::ScopedLock lock (stateLock);
            if (! ready.load (std::memory_order_acquire)
                || normalSelectionGeneration.load (std::memory_order_acquire)
                    != selectionGeneration
                || (expectedPlaybackIdentity.isNotEmpty()
                    && currentSnapshot.playbackIdentity != expectedPlaybackIdentity))
                return false;
            epoch = auditionEpoch.load (std::memory_order_acquire);
            comparisonMode = currentSnapshot.comparisonMode;
            source = publishedSource;
            cueLevel = currentSnapshot.cueLevelAvailable;
            cueLoudness = currentSnapshot.cueIntegratedLoudness;
            cuePeak = currentSnapshot.cueMaximumTruePeakDbtp;
        }
        if (source == nullptr)
            return false;
        // H4: B・C で鳴らすのは Cue なので、Kirin OS の Cue の値（ranges）があればそれで合わせる。無ければ
        // 曲全体の値（Kirin OS で測り直すと Cue の値になる）。V は曲全体の位置合わせの観測差で合わせる。
        const auto& summary = source->measurementSummary;
        const auto sourceLoudness = cueLevel && ! versionComparison ? cueLoudness
            : summary && summary->loudnessLufsI ? *summary->loudnessLufsI : unavailable();
        const auto sourcePeak = cueLevel && ! versionComparison && std::isfinite (cuePeak) ? cuePeak
            : summary && summary->maximumTruePeakDbtp ? *summary->maximumTruePeakDbtp : unavailable();
        const auto rejectMatch = [&] (MatchFailure reason)
        {
            const juce::ScopedLock lock (stateLock);
            if (normalSelectionGeneration.load (std::memory_order_acquire) == selectionGeneration)
            { currentSnapshot.matchFailure = reason; preparedNormalSelection.valid = false; }
            return false;
        };

        double requiredGain = 0.0;
        if (!versionComparison && comparisonMode == "loudness_match")
        {
            if (! std::isfinite (aIntegratedLoudness))
                return rejectMatch (MatchFailure::liveLevelUnavailable);
            if (! std::isfinite (sourceLoudness))
                return rejectMatch (MatchFailure::sourceLevelUnavailable);
            requiredGain = aIntegratedLoudness - sourceLoudness;
        }
        else if (!versionComparison && comparisonMode == "peak_match")
        {
            if (! std::isfinite (aMaximumTruePeakDbtp))
                return rejectMatch (MatchFailure::liveLevelUnavailable);
            if (! std::isfinite (sourcePeak))
                return rejectMatch (MatchFailure::sourceLevelUnavailable);
            requiredGain = aMaximumTruePeakDbtp - sourcePeak;
        }
        else if (!versionComparison && comparisonMode != "original")
            return false;
        if (versionComparison)
        {
            const auto calibration = blind.snapshot();
            if (!calibration.wholeSong || !calibration.eligible) return false;
            if (calibration.wholeSong && calibration.eligible)
            {
                requiredGain = calibration.pairedLoudnessDeltaDb;
                aMaximumTruePeakDbtp = calibration.aCueTruePeakDbtp;
                // The paired observation determines gain. It does not establish
                // an integrated whole-song LUFS value for the live, editable A.
                aIntegratedLoudness = unavailable();
            }
        }
        if (! std::isfinite (requiredGain)
            || requiredGain < -100.0 || requiredGain > 100.0)
            return false;

        const double appliedGain = requiredGain;
        if (requiredGain > 0.0)
        {
            if (! std::isfinite (sourcePeak))
                return rejectMatch (MatchFailure::sourceLevelUnavailable);
            if (referenceGainHeadroomDb (sourcePeak, aMaximumTruePeakDbtp) + 1.0e-9 < requiredGain)
                return rejectMatch (MatchFailure::ceilingExceeded);
        }

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
        // A normal MATCH is exact or rejected. Only explicit original mode uses zero gain.
        prepared.gainLimited = false;
        prepared.comparisonFallbackOriginal = false;
        // H3: V と、追従にした役（B）の知覚音量の MATCH は窓に追従する。Peak の MATCH と C は固定。
        prepared.tracking = ! versionComparison && comparisonMode == "original" ? TrackingState::none
            : versionComparison || (trackingEnabled.load (std::memory_order_acquire) && comparisonMode == "loudness_match")
                ? TrackingState::following : TrackingState::fixed;
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
                currentSnapshot.tracking = prepared.tracking;
                heldSelection = { currentSnapshot.playbackIdentity, prepared, true };  // H5
                preparedNormalSelection.valid = false;
            }
        }
        if (! publicationStillValid)
        {
            releaseOutputGate (gateToken);
            return false;
        }
        activeAuditionEpoch.store (epoch, std::memory_order_release);
        normalReturnToken.store (0, std::memory_order_release);
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
                                       double aMaximumTruePeakDbtp, std::uint64_t queuedGeneration,
                                       const juce::String& expectedPlaybackIdentity) noexcept
    {
        if (blind.ongoing())
            return false;
        if (bSelected.load (std::memory_order_acquire)) return true;
        const auto generation = queuedGeneration != 0 ? queuedGeneration
            : normalSelectionGeneration.fetch_add (1, std::memory_order_acq_rel) + 1;
        const bool alreadySelected = bSelected.load (std::memory_order_acquire);
        const auto bBaseline = bAudibleConfirmations.load (std::memory_order_acquire);
        { const juce::ScopedLock lock (stateLock); currentSnapshot.matchFailure = MatchFailure::none; }
        if (!prepareReferenceGain (aIntegratedLoudness, aMaximumTruePeakDbtp, generation,
                                    expectedPlaybackIdentity)) return false;
        const bool selected = activatePreparedB (generation);
        if (selected && ! alreadySelected)
            beginAuditionEventSession (bBaseline);
        return selected;
    }

    void RuntimeV2Controller::selectA (bool allowFade) noexcept
    {
        normalSelectionGeneration.fetch_add (1, std::memory_order_acq_rel);
        if (blind.ongoing())
        {
            endBlind();
            return;
        }
        const auto aBaseline = aAudibleConfirmations.load (std::memory_order_acquire);
        const bool wasSelected = bSelected.exchange (false, std::memory_order_acq_rel);
        const bool fade = allowFade && wasSelected && normalAudible.load (std::memory_order_acquire)
            && ready.load (std::memory_order_acquire) && latestPlaying.load (std::memory_order_acquire);
        if (fade) normalReturnToken.store (activeOutputGateToken.load (std::memory_order_acquire), std::memory_order_release);
        else if (allowFade && normalReturnToken.load (std::memory_order_acquire)
                 && ready.load (std::memory_order_acquire) && latestPlaying.load (std::memory_order_acquire)) return;
        else { normalReturnToken.store (0, std::memory_order_release); activeAuditionEpoch.store (0, std::memory_order_release); }
        const bool deferredReturn = auditionReturnPending.exchange (
            false, std::memory_order_acq_rel);
        const auto deferredRelease = normalGateReleasePendingToken.exchange (
            0, std::memory_order_acq_rel);
        if (wasSelected || deferredReturn)
            requestAuditionReturnEvent (aBaseline);
        if (!fade && !normalReturnToken.load (std::memory_order_acquire))
            releaseActiveOutputGate();
        else
            releaseOutputGate (deferredRelease);
    }
}
