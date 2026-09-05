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

        bool checkedAdd (std::int64_t left, std::int64_t right,
                         std::int64_t& result) noexcept
        {
            if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
                || (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
                return false;
            result = left + right;
            return true;
        }

        bool checkedSubtract (std::int64_t left, std::int64_t right,
                              std::int64_t& result) noexcept
        {
            if ((right < 0 && left > std::numeric_limits<std::int64_t>::max() + right)
                || (right > 0 && left < std::numeric_limits<std::int64_t>::min() + right))
                return false;
            result = left - right;
            return true;
        }

        std::int64_t positiveModulo (std::int64_t value,
                                     std::int64_t modulus) noexcept
        {
            const auto remainder = value % modulus;
            return remainder < 0 ? remainder + modulus : remainder;
        }

        std::int64_t wrappedDifference (std::int64_t value, std::int64_t origin,
                                        std::int64_t modulus) noexcept
        {
            const auto valueRemainder = positiveModulo (value, modulus);
            const auto originRemainder = positiveModulo (origin, modulus);
            return valueRemainder >= originRemainder
                ? valueRemainder - originRemainder
                : modulus - (originRemainder - valueRemainder);
        }
    }

    bool RuntimeV2Controller::requestSelection (const juce::String& kind,
                                                const juce::String& id)
    {
        if (id.isEmpty())
            return false;
        {
            const juce::ScopedLock lock (stateLock);
            if (kind == "preset") requestedSelection.presetId = id;
            else if (kind == "check") requestedSelection.checkId = id;
            else if (kind == "candidate") requestedSelection.candidateId = id;
            else if (kind == "cue") requestedSelection.cueId = id;
            else return false;
            ++requestedSelection.generation;
            requestedSelection.sampleRateApprovalKey.clear();
        }
        selectA();
        notify();
        return true;
    }

    bool RuntimeV2Controller::selectPreset (const juce::String& id)
    {
        return requestSelection ("preset", id);
    }

    bool RuntimeV2Controller::selectCheck (const juce::String& id)
    {
        return requestSelection ("check", id);
    }

    bool RuntimeV2Controller::selectCandidate (const juce::String& id)
    {
        return requestSelection ("candidate", id);
    }

    bool RuntimeV2Controller::selectCue (const juce::String& id)
    {
        return requestSelection ("cue", id);
    }

    bool RuntimeV2Controller::approveSampleRateConversion()
    {
        const juce::ScopedLock lock (stateLock);
        if (pendingApprovalKey.isEmpty())
            return false;
        requestedSelection.sampleRateApprovalKey = pendingApprovalKey;
        ++requestedSelection.generation;
        notify();
        return true;
    }

    bool RuntimeV2Controller::requestRecovery()
    {
        RecoveryAuthority authority;
        RecoveryContext context;
        RecoveryDestination destination = RecoveryDestination::reference;
        {
            const juce::ScopedLock lock (stateLock);
            if (pendingRecoveryRequest.has_value()) return true;
            authority.runtimeInstanceId = requestedConfiguration.identity.runtimeInstanceId;
            authority.hostProcessId = requestedConfiguration.identity.hostProcessId;
            authority.workId = requestedConfiguration.identity.workId;
            context = { currentSnapshot.presetId, currentSnapshot.checkId,
                        currentSnapshot.candidateId };
            if (currentSnapshot.rejectionCode.contains ("source"))
                destination = RecoveryDestination::candidateSource;
            else if (! currentSnapshot.measurementAvailable
                     && ! currentSnapshot.viewBindings.empty()
                     && currentSnapshot.candidateId.isNotEmpty())
                destination = RecoveryDestination::candidateMeasurement;
        }
        const auto request = recoveryTransport.writeRequest (
            authority, destination, context, juce::Time::currentTimeMillis());
        if (! request) return false;
        {
            const juce::ScopedLock lock (stateLock);
            pendingRecoveryRequest = request;
            currentSnapshot.recoveryStatus = "pending";
            recoveryStatusExpiresAtMs = 0;
        }
        notify();
        return true;
    }

    void RuntimeV2Controller::observeTransport (std::int64_t hostPosition,
                                                bool positionValid,
                                                bool playing) noexcept
    {
        latestPlaying.store (playing, std::memory_order_release);
        latestPositionValid.store (positionValid, std::memory_order_release);
        if (! positionValid)
            return;
        latestHostPosition.store (hostPosition, std::memory_order_release);
        const auto sourcePosition = mappedSourcePosition (hostPosition);
        if (sourcePosition >= 0)
            pages.request (sourcePosition);
    }

    void RuntimeV2Controller::observeAInput (const juce::AudioBuffer<float>& input,
                                             std::int64_t hostPosition,
                                             bool positionValid,
                                             bool playing,
                                             bool auditionAllowed) noexcept
    {
        aCapture.observe (input, hostPosition, positionValid, playing,
                          auditionAllowed
                              && ! bSelected.load (std::memory_order_acquire)
                              && ! blind.ongoing());
        if (auditionAllowed && playing && positionValid && input.getNumSamples() > 0
            && ! bSelected.load (std::memory_order_acquire) && ! blind.ongoing())
            aAudibleConfirmations.fetch_add (1, std::memory_order_release);
    }

    std::int64_t RuntimeV2Controller::mappedSourcePosition (
        std::int64_t hostPosition) const noexcept
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto generation = mappingGeneration.load (std::memory_order_acquire);
            if ((generation & 1u) != 0)
                continue;
            const auto start = cueStart.load (std::memory_order_relaxed);
            const auto end = cueEnd.load (std::memory_order_relaxed);
            const bool locked = sampleLocked.load (std::memory_order_relaxed);
            const bool loops = cueLoops.load (std::memory_order_relaxed);
            const auto hostAnchor = bHostAnchor.load (std::memory_order_relaxed);
            const auto sourceAnchor = bSourceAnchor.load (std::memory_order_relaxed);
            std::int64_t result = -1;
            if (end > start)
            {
                if (loops)
                {
                    const auto length = end - start;
                    if (locked)
                        result = start + wrappedDifference (hostPosition, start, length);
                    else
                    {
                        const auto hostOffset = wrappedDifference (
                            hostPosition, hostAnchor, length);
                        const auto sourceOffset = wrappedDifference (
                            sourceAnchor, start, length);
                        const auto offset = hostOffset >= length - sourceOffset
                            ? hostOffset - (length - sourceOffset)
                            : sourceOffset + hostOffset;
                        result = start + offset;
                    }
                }
                else if (locked)
                    result = hostPosition >= start && hostPosition < end
                        ? hostPosition : -1;
                else
                {
                    std::int64_t delta = 0;
                    std::int64_t position = 0;
                    if (sourceAnchor >= start && sourceAnchor < end
                        && checkedSubtract (hostPosition, hostAnchor, delta)
                        && checkedAdd (sourceAnchor, delta, position)
                        && position >= start && position < end)
                        result = position;
                }
            }
            if (mappingGeneration.load (std::memory_order_acquire) == generation)
                return result;
        }
        return -1;
    }

    bool RuntimeV2Controller::prepareReferenceGain (double aIntegratedLoudness,
                                                    double aMaximumTruePeakDbtp) noexcept
    {
        if (! ready.load (std::memory_order_acquire)
            || ! latestPositionValid.load (std::memory_order_acquire))
            return false;
        juce::String comparisonMode;
        std::shared_ptr<const RuntimeSource> source;
        {
            const juce::ScopedLock lock (stateLock);
            comparisonMode = currentSnapshot.comparisonMode;
            source = activeSource;
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
        if (! std::isfinite (requiredGain) || requiredGain < -100.0 || requiredGain > 100.0)
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
                    std::isfinite (aMaximumTruePeakDbtp) ? aMaximumTruePeakDbtp : -1.0,
                    *source->measurementSummary->maximumTruePeakDbtp);
                appliedGain = juce::jmax (0.0, juce::jmin (
                    requiredGain,
                    ceiling - *source->measurementSummary->maximumTruePeakDbtp));
            }
            limited = appliedGain + 1.0e-9 < requiredGain;
        }
        bLinearGain.store (gainFromDecibels (appliedGain), std::memory_order_release);
        const auto sourceLoudness = source->measurementSummary
            && source->measurementSummary->loudnessLufsI
            ? *source->measurementSummary->loudnessLufsI : unavailable();
        const auto sourcePeak = source->measurementSummary
            && source->measurementSummary->maximumTruePeakDbtp
            ? *source->measurementSummary->maximumTruePeakDbtp : unavailable();
        {
            const juce::ScopedLock lock (stateLock);
            currentSnapshot.appliedGainDb = appliedGain;
            currentSnapshot.gainLimited = limited;
            currentSnapshot.comparisonFallbackOriginal = fallbackOriginal;
            currentSnapshot.aIntegratedLoudness = aIntegratedLoudness;
            currentSnapshot.aMaximumTruePeakDbtp = aMaximumTruePeakDbtp;
            currentSnapshot.adjustedBIntegratedLoudness = std::isfinite (sourceLoudness)
                ? sourceLoudness + appliedGain : unavailable();
            currentSnapshot.adjustedBMaximumTruePeakDbtp = std::isfinite (sourcePeak)
                ? sourcePeak + appliedGain : unavailable();
            currentSnapshot.loudnessDeltaBMinusA = std::isfinite (aIntegratedLoudness)
                && std::isfinite (currentSnapshot.adjustedBIntegratedLoudness)
                ? currentSnapshot.adjustedBIntegratedLoudness - aIntegratedLoudness : unavailable();
            currentSnapshot.truePeakDeltaBMinusA = std::isfinite (aMaximumTruePeakDbtp)
                && std::isfinite (currentSnapshot.adjustedBMaximumTruePeakDbtp)
                ? currentSnapshot.adjustedBMaximumTruePeakDbtp - aMaximumTruePeakDbtp : unavailable();
        }
        return true;
    }

    bool RuntimeV2Controller::activatePreparedB() noexcept
    {
        if (! ready.load (std::memory_order_acquire)
            || ! latestPlaying.load (std::memory_order_acquire)
            || ! latestPositionValid.load (std::memory_order_acquire))
            return false;
        const auto sourcePosition = mappedSourcePosition (latestHostPosition.load());
        pages.request (sourcePosition);
        if (! pages.readyAt (sourcePosition, 1) || (selectionGate && ! selectionGate (true)))
            return false;
        bSelected.store (true, std::memory_order_release);
        return true;
    }

    bool RuntimeV2Controller::selectB (double aIntegratedLoudness,
                                       double aMaximumTruePeakDbtp) noexcept
    {
        if (blind.ongoing())
            return false;
        const bool alreadySelected = bSelected.load (std::memory_order_acquire);
        const auto bBaseline = bAudibleConfirmations.load (std::memory_order_acquire);
        const bool selected = prepareReferenceGain (aIntegratedLoudness, aMaximumTruePeakDbtp)
                           && activatePreparedB();
        if (selected && ! alreadySelected)
            beginAuditionEventSession (bBaseline);
        return selected;
    }

    void RuntimeV2Controller::selectA() noexcept
    {
        if (blind.ongoing())
        {
            endBlind();
            return;
        }
        const auto aBaseline = aAudibleConfirmations.load (std::memory_order_acquire);
        const bool wasSelected = bSelected.exchange (false, std::memory_order_acq_rel);
        const bool deferredReturn = auditionReturnPending.exchange (
            false, std::memory_order_acq_rel);
        const bool deferredRelease = gateReleasePending.exchange (
            false, std::memory_order_acq_rel);
        if (wasSelected || deferredReturn)
            requestAuditionReturnEvent (aBaseline);
        if ((wasSelected || deferredRelease) && selectionGate)
            selectionGate (false);
    }

    bool RuntimeV2Controller::startBlind (double aIntegratedLoudness,
                                          double aMaximumTruePeakDbtp) noexcept
    {
        juce::ignoreUnused (aMaximumTruePeakDbtp);
        if (! latestPlaying.load (std::memory_order_acquire)
            || ! latestPositionValid.load (std::memory_order_acquire)
            || blind.ongoing() || ! blind.start())
            return false;
        if (selectionGate && ! selectionGate (true))
        {
            blind.end();
            return false;
        }
        bSelected.store (false, std::memory_order_release);
        const auto facts = blind.snapshot();
        beginBlindEventSession (facts);
        const juce::ScopedLock lock (stateLock);
        currentSnapshot.aIntegratedLoudness = aIntegratedLoudness + facts.aGainDb;
        currentSnapshot.aMaximumTruePeakDbtp = facts.aCueTruePeakDbtp + facts.aGainDb;
        currentSnapshot.appliedGainDb = facts.bGainDb;
        currentSnapshot.adjustedBIntegratedLoudness = currentSnapshot.aIntegratedLoudness;
        currentSnapshot.adjustedBMaximumTruePeakDbtp = facts.bCueTruePeakDbtp + facts.bGainDb;
        currentSnapshot.loudnessDeltaBMinusA = 0.0;
        currentSnapshot.truePeakDeltaBMinusA = currentSnapshot.adjustedBMaximumTruePeakDbtp
                                             - currentSnapshot.aMaximumTruePeakDbtp;
        return true;
    }

    bool RuntimeV2Controller::approveBlindLowerAAndStart (
        double aIntegratedLoudness, double aMaximumTruePeakDbtp) noexcept
    {
        juce::ignoreUnused (aMaximumTruePeakDbtp);
        if (! latestPlaying.load (std::memory_order_acquire)
            || ! latestPositionValid.load (std::memory_order_acquire)
            || blind.ongoing() || ! blind.start (true))
            return false;
        if (selectionGate && ! selectionGate (true))
        {
            blind.end();
            return false;
        }
        bSelected.store (false, std::memory_order_release);
        const auto facts = blind.snapshot();
        beginBlindEventSession (facts);
        const juce::ScopedLock lock (stateLock);
        currentSnapshot.aIntegratedLoudness = aIntegratedLoudness + facts.aGainDb;
        currentSnapshot.aMaximumTruePeakDbtp = facts.aCueTruePeakDbtp + facts.aGainDb;
        currentSnapshot.appliedGainDb = facts.bGainDb;
        currentSnapshot.adjustedBIntegratedLoudness = currentSnapshot.aIntegratedLoudness;
        currentSnapshot.adjustedBMaximumTruePeakDbtp = facts.bCueTruePeakDbtp + facts.bGainDb;
        currentSnapshot.loudnessDeltaBMinusA = 0.0;
        currentSnapshot.truePeakDeltaBMinusA = currentSnapshot.adjustedBMaximumTruePeakDbtp
                                             - currentSnapshot.aMaximumTruePeakDbtp;
        return true;
    }

    bool RuntimeV2Controller::selectBlindStimulus (int stimulus) noexcept
    {
        return latestPlaying.load (std::memory_order_acquire)
            && latestPositionValid.load (std::memory_order_acquire)
            && blind.requestStimulus (stimulus);
    }

    bool RuntimeV2Controller::answerBlind (int stimulus) noexcept
    {
        return blind.answer (stimulus);
    }

    bool RuntimeV2Controller::revealBlind() noexcept
    {
        if (! blind.reveal()) return false;
        completeBlindEventSession (blind.snapshot());
        return true;
    }

    void RuntimeV2Controller::endBlind() noexcept
    {
        const bool wasOngoing = blind.ongoing();
        blind.end();
        bSelected.store (false, std::memory_order_release);
        const bool deferredRelease = gateReleasePending.exchange (
            false, std::memory_order_acq_rel);
        if ((wasOngoing || deferredRelease) && selectionGate)
            selectionGate (false);
    }

    void RuntimeV2Controller::loseAudibleConfirmation() noexcept
    {
        blind.loseAudibleConfirmation();
    }

    void RuntimeV2Controller::invalidateBlind() noexcept
    {
        const bool wasOngoing = blind.ongoing();
        blind.invalidate();
        bSelected.store (false, std::memory_order_release);
        if (wasOngoing && selectionGate)
            selectionGate (false);
    }

    void RuntimeV2Controller::failClosedToA() noexcept
    {
        if (blind.ongoing())
            invalidateBlind();
        else
            selectA();
        ready.store (false, std::memory_order_release);
    }

    void RuntimeV2Controller::invalidateBlindFromAudioThread() noexcept
    {
        const bool wasOngoing = blind.ongoing();
        blind.invalidate();
        bSelected.store (false, std::memory_order_release);
        if (wasOngoing)
            gateReleasePending.store (true, std::memory_order_release);
    }

    void RuntimeV2Controller::failClosedToAFromAudioThread() noexcept
    {
        ready.store (false, std::memory_order_release);
        if (blind.ongoing())
        {
            invalidateBlindFromAudioThread();
        }
        else if (bSelected.exchange (false, std::memory_order_acq_rel))
        {
            auditionReturnPending.store (true, std::memory_order_release);
            gateReleasePending.store (true, std::memory_order_release);
        }
    }

}
