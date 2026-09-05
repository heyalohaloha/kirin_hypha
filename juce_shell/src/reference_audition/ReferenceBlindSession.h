#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hypha::reference_audition
{
    bool secureRandomBit();
    void secureRandomBytes (void* destination, std::size_t byteCount);

    enum class BlindSide : int
    {
        a = 0,
        b = 1,
    };

    enum class BlindPhase : int
    {
        inactive = 0,
        active = 1,
        revealed = 2,
        invalidated = 3,
    };

    struct BlindPublicState
    {
        BlindPhase phase = BlindPhase::inactive;
        int activeStimulus = 0;
        int pendingStimulus = 0;
        int revealedStimulusOneSide = -1;
    };

    class BlindSession final
    {
    public:
        void begin (bool stimulusOneUsesB) noexcept;
        bool request (int stimulus, BlindSide& requestedSide) noexcept;
        void cancelPendingRequest() noexcept;
        void confirmAudible (BlindSide side) noexcept;
        void loseAudibleConfirmation() noexcept;
        bool reveal() noexcept;
        void end() noexcept;
        void invalidate() noexcept;

        BlindPublicState publicState() const noexcept;
        bool ongoing() const noexcept;

    private:
        BlindSide sideForStimulus (int stimulus) const noexcept;

        std::atomic<int> phase { static_cast<int> (BlindPhase::inactive) };
        std::atomic<bool> stimulusOneIsB { false };
        std::atomic<int> requestedStimulus { 0 };
        std::atomic<int> requestedSide { static_cast<int> (BlindSide::a) };
        std::atomic<std::uint64_t> requestedSequence { 0 };
        std::atomic<std::uint64_t> confirmedSequence { 0 };
        std::atomic<int> confirmedStimulus { 0 };
    };
}
