#include "ReferenceRuntimeV2Blind.h"

namespace hypha::reference_audition
{
    bool RuntimeV2Blind::enterPreparation() noexcept
    {
        if (attenuationHoldActive.load (std::memory_order_acquire))
            return false;
        auto previous = lifecycle.load (std::memory_order_acquire);
        while (previous != active && previous != revealed && previous != preparing)
        {
            if (lifecycle.compare_exchange_weak (previous, preparing,
                                                 std::memory_order_acq_rel))
            {
                while (callbacksInFlight.load (std::memory_order_acquire) != 0
                       || snapshotReadersInFlight.load (std::memory_order_acquire) != 0)
                    juce::Thread::yield();
                if (attenuationHoldActive.load (std::memory_order_acquire))
                {
                    int expected = preparing;
                    lifecycle.compare_exchange_strong (expected, previous,
                                                        std::memory_order_acq_rel);
                    return false;
                }
                return lifecycle.load (std::memory_order_acquire) == preparing;
            }
        }
        return false;
    }

    bool RuntimeV2Blind::start (bool approveLowerA) noexcept
    {
        int previous = approveLowerA ? approvalRequired : prepared;
        if (! lifecycle.compare_exchange_strong (previous, preparing,
                                                  std::memory_order_acq_rel))
            return false;
        while (snapshotReadersInFlight.load (std::memory_order_acquire) != 0)
            juce::Thread::yield();
        if (approveLowerA)
        {
            aGainDb = -requiredAAttenuationDb;
            bGainDb = 0.0;
        }
        normalReturnRequired.store (false, std::memory_order_release);
        resetSession();
        try
        {
            const auto commitment = createRuntimeV2BlindCommitment();
            trialId = commitment.trialId;
            assignmentNonceHex = commitment.nonceHex;
            assignmentCommitmentSha256 = commitment.commitmentSha256;
            stimulusOneIsB.store (commitment.stimulusOneIsB, std::memory_order_relaxed);
        }
        catch (...)
        {
            restorePreparedGain();
            int preparingState = preparing;
            lifecycle.compare_exchange_strong (preparingState, previous,
                                                std::memory_order_acq_rel);
            return false;
        }
        requestedStimulus.store (1, std::memory_order_relaxed);
        requestSequence.store (1, std::memory_order_release);
        int preparingState = preparing;
        return lifecycle.compare_exchange_strong (preparingState, active,
                                                   std::memory_order_acq_rel);
    }

    bool RuntimeV2Blind::cancelUnheardStart() noexcept
    {
        if (attenuationHoldActive.load (std::memory_order_acquire)
            || normalReturnRequired.load (std::memory_order_acquire)
            || callbackSequence.load (std::memory_order_acquire) != 0)
            return false;
        auto previous = lifecycle.load (std::memory_order_acquire);
        while (previous == active || previous == invalidated)
            if (lifecycle.compare_exchange_weak (previous, preparing,
                                                 std::memory_order_acq_rel))
            {
                while (callbacksInFlight.load (std::memory_order_acquire) != 0
                       || snapshotReadersInFlight.load (std::memory_order_acquire) != 0)
                    juce::Thread::yield();
                if (attenuationHoldActive.load (std::memory_order_acquire)
                    || normalReturnRequired.load (std::memory_order_acquire)
                    || callbackSequence.load (std::memory_order_acquire) != 0)
                {
                    lifecycle.store (invalidated, std::memory_order_release);
                    return false;
                }
                const auto target = requiredAAttenuationDb > 0.0
                    ? approvalRequired : prepared;
                restorePreparedGain();
                lifecycle.store (target, std::memory_order_release);
                resetSession();
                return true;
            }
        return false;
    }

    void RuntimeV2Blind::end() noexcept
    {
        auto previous = lifecycle.load (std::memory_order_acquire);
        if (previous == returnRequested || previous == normalConfirmed)
            return;
        if ((previous == active || previous == revealed || previous == invalidated)
            && cancelUnheardStart())
            return;
        previous = lifecycle.load (std::memory_order_acquire);
        if (previous != active && previous != revealed && previous != invalidated
            && ! attenuationHoldActive.load (std::memory_order_acquire))
        {
            resetSession();
            return;
        }

        const auto target = previous == invalidated
            ? returnLifecycle.load (std::memory_order_acquire)
            : previous == unavailable ? unavailable
            : requiredAAttenuationDb > 0.0 ? approvalRequired : prepared;
        returnLifecycle.store (target, std::memory_order_release);
        while (previous != preparing && previous != returnRequested
               && previous != normalConfirmed)
            if (lifecycle.compare_exchange_weak (previous, returnRequested,
                                                 std::memory_order_acq_rel))
                return;
    }

    bool RuntimeV2Blind::completeNormalReturn() noexcept
    {
        int expected = normalConfirmed;
        if (! lifecycle.compare_exchange_strong (expected, preparing,
                                                  std::memory_order_acq_rel))
            return false;
        attenuationHoldActive.store (false, std::memory_order_release);
        normalReturnRequired.store (false, std::memory_order_release);
        while (callbacksInFlight.load (std::memory_order_acquire) != 0
               || snapshotReadersInFlight.load (std::memory_order_acquire) != 0)
            juce::Thread::yield();
        heldALinearGain.store (1.0f, std::memory_order_relaxed);
        const auto target = returnLifecycle.exchange (unavailable,
                                                      std::memory_order_acq_rel);
        if (target == approvalRequired) restorePreparedGain();
        int preparingState = preparing;
        lifecycle.compare_exchange_strong (preparingState, target,
                                            std::memory_order_acq_rel);
        resetSession();
        return true;
    }

    void RuntimeV2Blind::restorePreparedGain() noexcept
    {
        if (requiredAAttenuationDb <= 0.0)
            return;
        aGainDb = 0.0;
        bGainDb = requiredAAttenuationDb;
    }

    void RuntimeV2Blind::forceClearAfterAudioStopped() noexcept
    {
        lifecycle.store (preparing, std::memory_order_release);
        while (callbacksInFlight.load (std::memory_order_acquire) != 0
               || snapshotReadersInFlight.load (std::memory_order_acquire) != 0)
            juce::Thread::yield();
        heldALinearGain.store (1.0f, std::memory_order_relaxed);
        attenuationHoldActive.store (false, std::memory_order_release);
        normalReturnRequired.store (false, std::memory_order_release);
        returnLifecycle.store (unavailable, std::memory_order_release);
        lifecycle.store (unavailable, std::memory_order_release);
        resetSession();
    }

    void RuntimeV2Blind::invalidate() noexcept
    {
        returnLifecycle.store (requiredAAttenuationDb > 0.0
                                   ? approvalRequired : prepared,
                               std::memory_order_release);
        int state = active;
        if (! lifecycle.compare_exchange_strong (state, invalidated,
                                                  std::memory_order_acq_rel))
        {
            state = revealed;
            lifecycle.compare_exchange_strong (state, invalidated,
                                                std::memory_order_acq_rel);
        }
    }

    void RuntimeV2Blind::clear() noexcept
    {
        if (attenuationHoldActive.load (std::memory_order_acquire)
            || normalReturnRequired.load (std::memory_order_acquire))
        {
            returnLifecycle.store (unavailable, std::memory_order_release);
            int state = active;
            if (! lifecycle.compare_exchange_strong (state, invalidated,
                                                      std::memory_order_acq_rel))
            {
                state = revealed;
                lifecycle.compare_exchange_strong (state, invalidated,
                                                    std::memory_order_acq_rel);
            }
        }
        else
            lifecycle.store (unavailable, std::memory_order_release);
        resetSession();
    }

    void RuntimeV2Blind::loseAudibleConfirmation() noexcept
    {
        activeStimulus.store (0, std::memory_order_release);
        confirmedSequence.store (0, std::memory_order_release);
    }
}
