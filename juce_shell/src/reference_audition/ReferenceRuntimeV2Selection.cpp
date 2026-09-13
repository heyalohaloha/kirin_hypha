#include "ReferenceRuntimeV2Controller.h"

#include <limits>

namespace hypha::reference_audition
{
    namespace
    {
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

    void RuntimeV2Controller::observeTransport (std::int64_t hostPosition,
                                                bool positionValid,
                                                bool playing) noexcept
    {
        latestPlaying.store (playing, std::memory_order_release);
        latestPositionValid.store (positionValid, std::memory_order_release);
        transportHeartbeat.fetch_add (1, std::memory_order_release);
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
                                             bool auditionAllowed, bool confirmAudible) noexcept
    {
        aCapture.observe (input, hostPosition, positionValid, playing,
                          auditionAllowed
                              && ! bSelected.load (std::memory_order_acquire)
                              && ! blind.ongoing());
        if (confirmAudible && auditionAllowed && playing && positionValid && input.getNumSamples() > 0
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

    bool RuntimeV2Controller::startBlind (double aIntegratedLoudness,
                                          double aMaximumTruePeakDbtp) noexcept
    {
        juce::ignoreUnused (aMaximumTruePeakDbtp);
        return startBlindWithApproval (aIntegratedLoudness, false);
    }

    bool RuntimeV2Controller::approveBlindLowerAAndStart (
        double aIntegratedLoudness, double aMaximumTruePeakDbtp) noexcept
    {
        juce::ignoreUnused (aMaximumTruePeakDbtp);
        return startBlindWithApproval (aIntegratedLoudness, true);
    }

    bool RuntimeV2Controller::startBlindWithApproval (
        double aIntegratedLoudness, bool approveLowerA) noexcept
    {
        normalSelectionGeneration.fetch_add (1, std::memory_order_acq_rel);
        if (! ready.load (std::memory_order_acquire)
            || ! latestPlaying.load (std::memory_order_acquire)
            || ! latestPositionValid.load (std::memory_order_acquire)
            || bSelected.load (std::memory_order_acquire) || blind.ongoing())
            return false;
        const auto epoch = auditionEpoch.load (std::memory_order_acquire);
        const auto gateToken = acquireOutputGate();
        if (gateToken == 0)
            return false;
        if (! ready.load (std::memory_order_acquire)
            || auditionEpoch.load (std::memory_order_acquire) != epoch
            || ! blind.startSession (approveLowerA, epoch, gateToken))
        {
            releaseOutputGate (gateToken);
            return false;
        }
        if (! ready.load (std::memory_order_acquire)
            || auditionEpoch.load (std::memory_order_acquire) != epoch)
        {
            blind.invalidate();
            ReferenceSessionRetirement retirement;
            if (blind.cancelUnheardStart (retirement))
            {
                releaseOutputGate (retirement.outputGateToken);
            }
            return false;
        }
        bSelected.store (false, std::memory_order_release);
        const auto facts = blind.snapshot();
        bool publicationStillValid = false;
        {
            const juce::ScopedLock lock (stateLock);
            publicationStillValid = ready.load (std::memory_order_acquire)
                && auditionEpoch.load (std::memory_order_acquire) == epoch;
            if (publicationStillValid)
            {
                currentSnapshot.aIntegratedLoudness = aIntegratedLoudness + facts.aGainDb;
                currentSnapshot.aMaximumTruePeakDbtp = facts.aCueTruePeakDbtp + facts.aGainDb;
                currentSnapshot.appliedGainDb = facts.bGainDb;
                currentSnapshot.adjustedBIntegratedLoudness = currentSnapshot.aIntegratedLoudness;
                currentSnapshot.adjustedBMaximumTruePeakDbtp = facts.bCueTruePeakDbtp + facts.bGainDb;
                currentSnapshot.loudnessDeltaBMinusA = 0.0;
                currentSnapshot.truePeakDeltaBMinusA = currentSnapshot.adjustedBMaximumTruePeakDbtp
                                                     - currentSnapshot.aMaximumTruePeakDbtp;
            }
        }
        if (! publicationStillValid)
        {
            blind.invalidate();
            ReferenceSessionRetirement retirement;
            if (blind.cancelUnheardStart (retirement))
            {
                releaseOutputGate (retirement.outputGateToken);
            }
            return false;
        }
        beginBlindEventSession (facts);
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
        ReferenceSessionRetirement cancelled;
        if (blind.cancelUnheardStart (cancelled))
            releaseOutputGate (cancelled.outputGateToken);
        else
            blind.end();
        bSelected.store (false, std::memory_order_release);
    }

    void RuntimeV2Controller::suspendAudition() noexcept
    {
        if (blind.ongoing())
            invalidateBlind();
        else
            selectA();
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
        ReferenceSessionRetirement retirement;
        if (wasOngoing && blind.cancelUnheardStart (retirement))
        {
            releaseOutputGate (retirement.outputGateToken);
        }
    }

    void RuntimeV2Controller::failClosedToA() noexcept
    {
        revokeAuditionPublication();
        if (blind.ongoing())
            invalidateBlind();
        else
            selectA();
    }

    void RuntimeV2Controller::invalidateBlindFromAudioThread() noexcept
    {
        const bool wasOngoing = blind.ongoing();
        blind.invalidate();
        bSelected.store (false, std::memory_order_release);
        if (wasOngoing && ! blind.ongoing())
        {
            blindGateReleasePendingToken.store (
                blind.activeOutputGateToken(),
                std::memory_order_release);
        }
    }

    void RuntimeV2Controller::failClosedToAFromAudioThread() noexcept
    {
        revokeAuditionPublication();
        if (blind.ongoing())
        {
            invalidateBlindFromAudioThread();
        }
        else
        {
            const auto gateToken = activeOutputGateToken.load (std::memory_order_acquire);
            if (bSelected.exchange (false, std::memory_order_acq_rel))
            {
                activeAuditionEpoch.store (0, std::memory_order_release);
                auditionReturnPending.store (true, std::memory_order_release);
                normalGateReleasePendingToken.store (gateToken, std::memory_order_release);
            }
        }
    }

}
