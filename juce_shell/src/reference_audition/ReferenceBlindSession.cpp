#include "ReferenceBlindSession.h"

#include <cstdint>
#include <random>
#include <stdexcept>

#if defined(_WIN32)
 #include <bcrypt.h>
#elif defined(__APPLE__)
 #include <Security/SecRandom.h>
#endif

namespace hypha::reference_audition
{
    namespace
    {
        bool acceptsRequests (BlindPhase phase) noexcept
        {
            return phase == BlindPhase::active || phase == BlindPhase::revealed;
        }
    }

    bool secureRandomBit()
    {
        std::uint8_t value = 0;
       #if defined(_WIN32)
        if (::BCryptGenRandom (nullptr, &value, sizeof (value),
                               BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
            throw std::runtime_error ("system random unavailable");
       #elif defined(__APPLE__)
        if (::SecRandomCopyBytes (kSecRandomDefault, sizeof (value), &value) != errSecSuccess)
            throw std::runtime_error ("system random unavailable");
       #else
        std::random_device source;
        value = static_cast<std::uint8_t> (source());
       #endif
        return (value & 1u) != 0u;
    }

    void BlindSession::begin (bool stimulusOneUsesB) noexcept
    {
        stimulusOneIsB.store (stimulusOneUsesB, std::memory_order_relaxed);
        requestedStimulus.store (0, std::memory_order_relaxed);
        confirmedStimulus.store (0, std::memory_order_relaxed);
        requestedSequence.store (0, std::memory_order_relaxed);
        confirmedSequence.store (0, std::memory_order_relaxed);
        phase.store (static_cast<int> (BlindPhase::active), std::memory_order_release);
    }

    BlindSide BlindSession::sideForStimulus (int stimulus) const noexcept
    {
        const bool firstIsB = stimulusOneIsB.load (std::memory_order_relaxed);
        return (stimulus == 1) == firstIsB ? BlindSide::b : BlindSide::a;
    }

    bool BlindSession::request (int stimulus, BlindSide& side) noexcept
    {
        const auto currentPhase = static_cast<BlindPhase> (
            phase.load (std::memory_order_acquire));
        if (! acceptsRequests (currentPhase) || (stimulus != 1 && stimulus != 2))
            return false;

        side = sideForStimulus (stimulus);
        requestedStimulus.store (stimulus, std::memory_order_relaxed);
        requestedSide.store (static_cast<int> (side), std::memory_order_relaxed);
        const auto next = requestedSequence.load (std::memory_order_relaxed) + 1;
        requestedSequence.store (next, std::memory_order_release);
        return true;
    }

    void BlindSession::cancelPendingRequest() noexcept
    {
        requestedStimulus.store (0, std::memory_order_relaxed);
        confirmedStimulus.store (0, std::memory_order_relaxed);
        confirmedSequence.store (0, std::memory_order_release);
        requestedSequence.fetch_add (1, std::memory_order_release);
    }

    void BlindSession::confirmAudible (BlindSide side) noexcept
    {
        const auto currentPhase = static_cast<BlindPhase> (
            phase.load (std::memory_order_acquire));
        if (! acceptsRequests (currentPhase))
            return;
        const auto sequence = requestedSequence.load (std::memory_order_acquire);
        if (sequence == 0
            || requestedSide.load (std::memory_order_relaxed) != static_cast<int> (side))
            return;
        confirmedStimulus.store (requestedStimulus.load (std::memory_order_relaxed),
                                 std::memory_order_relaxed);
        confirmedSequence.store (sequence, std::memory_order_release);
    }

    void BlindSession::loseAudibleConfirmation() noexcept
    {
        confirmedSequence.store (0, std::memory_order_release);
        confirmedStimulus.store (0, std::memory_order_relaxed);
    }

    bool BlindSession::reveal() noexcept
    {
        int expected = static_cast<int> (BlindPhase::active);
        return phase.compare_exchange_strong (
            expected, static_cast<int> (BlindPhase::revealed),
            std::memory_order_acq_rel, std::memory_order_acquire);
    }

    void BlindSession::end() noexcept
    {
        phase.store (static_cast<int> (BlindPhase::inactive), std::memory_order_release);
        cancelPendingRequest();
    }

    void BlindSession::invalidate() noexcept
    {
        if (ongoing())
            phase.store (static_cast<int> (BlindPhase::invalidated),
                         std::memory_order_release);
        cancelPendingRequest();
    }

    BlindPublicState BlindSession::publicState() const noexcept
    {
        BlindPublicState state;
        state.phase = static_cast<BlindPhase> (phase.load (std::memory_order_acquire));
        const auto requested = requestedSequence.load (std::memory_order_acquire);
        const auto confirmed = confirmedSequence.load (std::memory_order_acquire);
        const auto stimulus = requestedStimulus.load (std::memory_order_relaxed);
        if (stimulus != 0 && requested == confirmed)
            state.activeStimulus = confirmedStimulus.load (std::memory_order_relaxed);
        else if (stimulus != 0)
            state.pendingStimulus = stimulus;
        if (state.phase == BlindPhase::revealed)
            state.revealedStimulusOneSide = stimulusOneIsB.load (std::memory_order_relaxed)
                ? static_cast<int> (BlindSide::b) : static_cast<int> (BlindSide::a);
        return state;
    }

    bool BlindSession::ongoing() const noexcept
    {
        return acceptsRequests (static_cast<BlindPhase> (
            phase.load (std::memory_order_acquire)));
    }
}
