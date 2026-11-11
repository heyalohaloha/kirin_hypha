#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceTrackingGain.h"
#include "ReferenceCopyCorrespondence.h"

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

    RuntimeV2Controller::MatchLevels RuntimeV2Controller::matchLevels (
        const RuntimeSource& source, bool cueLevel, double cueLoudness, double cuePeak, double loudnessOffset) const noexcept
    {
        const auto& summary = source.measurementSummary;
        return { loudnessOffset + (cueLevel && ! versionComparison ? cueLoudness
                     : summary && summary->loudnessLufsI ? *summary->loudnessLufsI : unavailable()),
                 cueLevel && ! versionComparison && std::isfinite (cuePeak) ? cuePeak
                     : summary && summary->maximumTruePeakDbtp ? *summary->maximumTruePeakDbtp : unavailable() };
    }

    MatchFailure RuntimeV2Controller::requiredMatchGain (const juce::String& mode, const MatchLevels& levels,
        double aIntegratedLoudness, double aMaximumTruePeakDbtp, double& requiredGain) const noexcept
    {
        requiredGain = 0.0;
        if (mode == "loudness_match")
        {
            if (! std::isfinite (aIntegratedLoudness)) return MatchFailure::liveLevelUnavailable;
            if (! std::isfinite (levels.loudness)) return MatchFailure::sourceLevelUnavailable;
            requiredGain = aIntegratedLoudness - levels.loudness;
        }
        else if (mode == "peak_match")
        {
            if (! std::isfinite (aMaximumTruePeakDbtp)) return MatchFailure::liveLevelUnavailable;
            if (! std::isfinite (levels.peak)) return MatchFailure::sourceLevelUnavailable;
            requiredGain = aMaximumTruePeakDbtp - levels.peak;
        }
        return MatchFailure::none;
    }

    // The gain and facts of one MATCH, computed once for a held audition and for a paired song switch.
    bool RuntimeV2Controller::matchedSelection (const juce::String& comparisonMode, double requiredGain, const MatchLevels& levels,
        double aIntegratedLoudness, double aMaximumTruePeakDbtp, PreparedNormalSelection& prepared, MatchFailure& failure) const noexcept
    {
        if (! std::isfinite (requiredGain)
            || requiredGain < -100.0 || requiredGain > 100.0)
            return false;

        double appliedGain = requiredGain, shortfall = 0.0;
        // 上限は、承認して A を下げている量を足した後の音で見る。届かなければ上限まで上げて鳴らし（足りない量を言う）、
        // 0.5 dB を超えるなら、承認すれば合わせられる下げ幅を添える。
        const auto held = heldAttenuationDb.load (std::memory_order_acquire);
        if (requiredGain + held > 0.0)
        {
            if (! std::isfinite (levels.peak))
            {
                failure = MatchFailure::sourceLevelUnavailable;
                return false;
            }
            shortfall = referencePeakShortfallDb (requiredGain, levels.peak, aMaximumTruePeakDbtp, held);
            appliedGain = requiredGain - shortfall;
        }

        prepared.linearGain = gainFromDecibels (appliedGain);
        prepared.appliedGainDb = appliedGain;
        prepared.anchorGainDb = appliedGain;
        prepared.aIntegratedLoudness = aIntegratedLoudness;
        prepared.aMaximumTruePeakDbtp = aMaximumTruePeakDbtp;
        prepared.adjustedBIntegratedLoudness = std::isfinite (levels.loudness)
            ? levels.loudness + appliedGain : unavailable();
        prepared.adjustedBMaximumTruePeakDbtp = std::isfinite (levels.peak)
            ? levels.peak + appliedGain : unavailable();
        prepared.loudnessDeltaBMinusA = std::isfinite (aIntegratedLoudness)
            && std::isfinite (prepared.adjustedBIntegratedLoudness)
            ? prepared.adjustedBIntegratedLoudness - aIntegratedLoudness : unavailable();
        prepared.truePeakDeltaBMinusA = std::isfinite (aMaximumTruePeakDbtp)
            && std::isfinite (prepared.adjustedBMaximumTruePeakDbtp)
            ? prepared.adjustedBMaximumTruePeakDbtp - aMaximumTruePeakDbtp : unavailable();
        // A normal MATCH is exact or stops at the ceiling. Only explicit original mode uses zero gain.
        prepared.gainLimited = false;
        prepared.peakShortfallDb = shortfall;
        prepared.requiredGainDb = requiredGain;
        prepared.comparisonFallbackOriginal = false;
        // V と、追従にした役（B）の知覚音量の MATCH は窓に追従する。Peak の MATCH と C は固定。
        prepared.tracking = ! versionComparison && comparisonMode == "original" ? TrackingState::none
            : versionComparison || (trackingEnabled.load (std::memory_order_acquire) && comparisonMode == "loudness_match")
                ? TrackingState::following : TrackingState::fixed;
        prepared.valid = true;
        return true;
    }

    // stateLock を持って呼ぶ。掛ける gain と、画面の値・追従の中心・戻す控えを同じ MATCH にそろえる。
    void RuntimeV2Controller::applyPreparedFactsLocked (const PreparedNormalSelection& prepared) noexcept
    {
        currentSnapshot.appliedGainDb = prepared.appliedGainDb;
        noteShortfallLocked (prepared.peakShortfallDb, prepared.requiredGainDb);
        currentSnapshot.aIntegratedLoudness = prepared.aIntegratedLoudness;
        currentSnapshot.aMaximumTruePeakDbtp = prepared.aMaximumTruePeakDbtp;
        currentSnapshot.adjustedBIntegratedLoudness = prepared.adjustedBIntegratedLoudness;
        currentSnapshot.adjustedBMaximumTruePeakDbtp = prepared.adjustedBMaximumTruePeakDbtp;
        currentSnapshot.loudnessDeltaBMinusA = prepared.loudnessDeltaBMinusA;
        currentSnapshot.truePeakDeltaBMinusA = prepared.truePeakDeltaBMinusA;
        currentSnapshot.gainLimited = prepared.gainLimited;
        currentSnapshot.comparisonFallbackOriginal = prepared.comparisonFallbackOriginal;
        currentSnapshot.rangeShortfallDb = prepared.rangeShortfallDb;
        currentSnapshot.tracking = prepared.tracking;
        trackingAnchorDb = prepared.anchorGainDb;
        heldSelection = { currentSnapshot.playbackIdentity, prepared, true };
        preparedNormalSelection.valid = false;
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
        double cueLoudness = unavailable(), cuePeak = unavailable(), loudnessOffset = 0.0;
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
            loudnessOffset = currentSnapshot.sourceLoudnessOffsetLu;
        }
        if (source == nullptr)
            return false;
        // B・C で鳴らすのは Cue なので、Kirin OS の Cue の値（ranges）があればそれで合わせる。無ければ
        // 曲全体の値（Kirin OS で測り直すと Cue の値になる）。V は曲全体の位置合わせの観測差で合わせる。
        const auto levels = matchLevels (*source, cueLevel, cueLoudness, cuePeak, loudnessOffset);
        const auto rejectMatch = [&] (MatchFailure reason, double neededAttenuationDb = 0.0)
        {
            const juce::ScopedLock lock (stateLock);
            if (normalSelectionGeneration.load (std::memory_order_acquire) == selectionGeneration)
            {
                failMatchLocked (reason, neededAttenuationDb);
                preparedNormalSelection.valid = false;
            }
            return false;
        };
        PreparedNormalSelection prepared;
        double requiredGain = 0.0;
        if (versionComparison)
        {
            const auto calibration = blind.snapshot();
            if (!calibration.wholeSong || !calibration.eligible) return false;
            requiredGain = calibration.pairedLoudnessDeltaDb;
            aMaximumTruePeakDbtp = calibration.aCueTruePeakDbtp;
            // The paired observation determines gain. It does not establish
            // an integrated whole-song LUFS value for the live, editable A.
            aIntegratedLoudness = unavailable();
        }
        else
        {
            const auto failure = requiredMatchGain (comparisonMode, levels, aIntegratedLoudness, aMaximumTruePeakDbtp, requiredGain);
            if (failure != MatchFailure::none) return rejectMatch (failure);
            if (comparisonMode != "loudness_match" && comparisonMode != "peak_match" && comparisonMode != "original") return false;
        }
        MatchFailure failure = MatchFailure::none;
        if (! matchedSelection (comparisonMode, requiredGain, levels, aIntegratedLoudness, aMaximumTruePeakDbtp, prepared, failure))
            return failure == MatchFailure::none ? false : rejectMatch (failure);
        prepared.auditionEpoch = epoch;
        prepared.selectionGeneration = selectionGeneration;

        const juce::ScopedLock lock (stateLock);
        if (! ready.load (std::memory_order_acquire)
            || auditionEpoch.load (std::memory_order_acquire) != epoch
            || normalSelectionGeneration.load (std::memory_order_acquire)
                != selectionGeneration)
            return false;
        preparedNormalSelection = prepared;
        return true;
    }

    RuntimeV2Controller::StartResult RuntimeV2Controller::activatePreparedB (
        std::uint64_t selectionGeneration) noexcept
    {
        if (dataOnlySelection.load (std::memory_order_acquire)) return StartResult::refused;
        std::uint64_t epoch = 0;
        {
            const juce::ScopedLock lock (stateLock);
            if (! preparedNormalSelection.valid
                || preparedNormalSelection.selectionGeneration != selectionGeneration)
                return StartResult::stale;
            epoch = preparedNormalSelection.auditionEpoch;
        }
        if (! ready.load (std::memory_order_acquire)
            || auditionEpoch.load (std::memory_order_acquire) != epoch
            || normalSelectionGeneration.load (std::memory_order_acquire)
                != selectionGeneration)
            return StartResult::stale;
        if (! latestPlaying.load (std::memory_order_acquire)
            || ! latestPositionValid.load (std::memory_order_acquire))
            return StartResult::preparing;
        const auto observation = inputObservationGeneration.load (std::memory_order_acquire);
        const auto hostPosition = latestHostPosition.load (std::memory_order_acquire);
        restartCueAtPlayhead (hostPosition);
        const auto mapping = mappingGeneration.load (std::memory_order_acquire);
        if ((mapping & 1u) != 0) return StartResult::preparing;
        const auto sourcePosition = mappedSourcePosition (hostPosition);
        if (mapping != mappingGeneration.load (std::memory_order_acquire)) return StartResult::preparing;
        if (sourcePosition < 0 || sourcePosition >= currentPages().lengthInSamples())
        {
            if (versionComparison && ((observation & 1u) != 0
                || ! referenceCopyProofCurrent (versionComparison, copyCorrespondence.load (std::memory_order_acquire),
                    contentMappingContinuity.load (std::memory_order_acquire), aCapture.continuity())
                || mapping != mappingGeneration.load (std::memory_order_acquire)
                || observation != inputObservationGeneration.load (std::memory_order_acquire)))
                return StartResult::preparing;
            return StartResult::refused;
        }
        currentPages().request (sourcePosition);
        if (! currentPages().readyAt (sourcePosition, 1))
            return StartResult::preparing;
        const auto gateToken = acquireOutputGate();
        if (gateToken == 0)
            return StartResult::refused;

        bool publicationStillValid = false;
        {
            const juce::ScopedLock lock (stateLock);
            publicationStillValid = ! dataOnlySelection.load (std::memory_order_acquire) && ready.load (std::memory_order_acquire)
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
                bLinearGain.store (preparedNormalSelection.linearGain, std::memory_order_release);
                applyPreparedFactsLocked (PreparedNormalSelection (preparedNormalSelection));
            }
        }
        if (! publicationStillValid)
        {
            releaseOutputGate (gateToken);
            return StartResult::stale;
        }
        activeAuditionEpoch.store (epoch, std::memory_order_release);
        normalReturnToken.store (0, std::memory_order_release);
        publicationReturn.store (0, std::memory_order_release);
        bSelected.store (true, std::memory_order_release);
        if (! ready.load (std::memory_order_acquire)
            || auditionEpoch.load (std::memory_order_acquire) != epoch
            || normalSelectionGeneration.load (std::memory_order_acquire)
                != selectionGeneration)
        {
            bSelected.store (false, std::memory_order_release);
            activeAuditionEpoch.store (0, std::memory_order_release);
            releaseOutputGate (gateToken);
            return StartResult::stale;
        }
        return StartResult::started;
    }

    bool RuntimeV2Controller::selectB (double loudness, double peak, std::uint64_t generation,
                                      const juce::String& identity) noexcept
    {
        return startB (loudness, peak, generation, identity) == StartResult::started;
    }

    RuntimeV2Controller::StartResult RuntimeV2Controller::startB (
        double loudness, double peak, std::uint64_t queuedGeneration,
        const juce::String& expectedPlaybackIdentity) noexcept
    {
        if (blind.ongoing() || dataOnlySelection.load (std::memory_order_acquire)) return StartResult::refused;
        if (queuedGeneration == 0 && expectedPlaybackIdentity.isEmpty()
            && bSelected.load (std::memory_order_acquire)) return StartResult::started;
        const auto generation = queuedGeneration != 0 ? queuedGeneration
            : normalSelectionGeneration.fetch_add (1, std::memory_order_acq_rel) + 1;
        const auto currentAttempt = [&]
        {
            const juce::ScopedLock lock (stateLock);
            return normalSelectionGeneration.load (std::memory_order_acquire) == generation
                && (expectedPlaybackIdentity.isEmpty() || currentSnapshot.playbackIdentity == expectedPlaybackIdentity);
        };
        if (!currentAttempt()) return StartResult::stale;
        if (revokeAfterFade.load (std::memory_order_acquire)) return StartResult::preparing;
        if (bSelected.load (std::memory_order_acquire)) return StartResult::started;
        if (!ready.load (std::memory_order_acquire) || !latestPositionValid.load (std::memory_order_acquire))
            return StartResult::preparing;
        if (versionComparison && !blind.snapshot().eligible) return StartResult::preparing;
        const auto baseline = bAudibleConfirmations.load (std::memory_order_acquire);
        { const juce::ScopedLock lock (stateLock); beginMatchLocked(); }
        if (!prepareReferenceGain (loudness, peak, generation, expectedPlaybackIdentity))
        {
            if (!currentAttempt()) return StartResult::stale;
            return versionComparison && !blind.snapshot().eligible ? StartResult::preparing : StartResult::refused;
        }
        const auto result = activatePreparedB (generation);
        if (result == StartResult::started) beginAuditionEventSession (baseline);
        return result;
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
