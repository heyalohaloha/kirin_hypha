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

    void RuntimeV2Blind::end() noexcept
    {
        auto previous = lifecycle.load (std::memory_order_acquire);
        const bool releaseRequested = previous == active || previous == revealed
            || previous == invalidated
            || attenuationHoldActive.load (std::memory_order_acquire);
        if (! releaseRequested)
        {
            resetSession();
            return;
        }

        for (;;)
        {
            if (previous == preparing)
                return;
            if (lifecycle.compare_exchange_weak (previous, preparing,
                                                 std::memory_order_acq_rel))
                break;
        }

        attenuationHoldActive.store (false, std::memory_order_release);
        while (callbacksInFlight.load (std::memory_order_acquire) != 0
               || snapshotReadersInFlight.load (std::memory_order_acquire) != 0)
            juce::Thread::yield();
        heldALinearGain.store (1.0f, std::memory_order_relaxed);
        attenuationHoldActive.store (false, std::memory_order_release);
        int target = prepared;
        if (previous == unavailable)
            target = unavailable;
        else if (requiredAAttenuationDb > 0.0)
        {
            aGainDb = 0.0;
            bGainDb = requiredAAttenuationDb;
            target = approvalRequired;
        }
        int preparingState = preparing;
        lifecycle.compare_exchange_strong (preparingState, target,
                                            std::memory_order_acq_rel);
        resetSession();
    }

    void RuntimeV2Blind::invalidate() noexcept
    {
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
        lifecycle.store (unavailable, std::memory_order_release);
        resetSession();
    }

    void RuntimeV2Blind::loseAudibleConfirmation() noexcept
    {
        activeStimulus.store (0, std::memory_order_release);
        confirmedSequence.store (0, std::memory_order_release);
    }
}
