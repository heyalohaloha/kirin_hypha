#pragma once
#include "CandidateReceiver.h"
#include "CandidateReturn.h"
#include "../local_blind/RtPublicationSlot.h"
#include <string>

namespace hypha::live_compare
{
struct CandidateProcessorState
{
    local_blind::RtPublicationSlot<CandidateReceiver> receiver;
    local_blind::RtPublicationSlot<CandidateReturnObserver> returnObserver;
    std::string selectedPre, originalPre; // message thread, never serialized
    std::atomic<std::uint64_t> token { 0 }, trackHash { 0 };
    std::atomic<bool> isolated { false }, returning { false }, blindReturned { false };
    std::atomic<std::uint64_t> quietFrames { 0 }, quietContext { 0 }, returnContext { 1 };
    // END starts one overall deadline for the whole return, as a waiting press keeps its 10 s:
    // a stop or seek restarts the four-second count, never this deadline.
    static constexpr std::uint64_t returnLimitNanos = 10'000'000'000ull;
    std::atomic<std::uint64_t> returnSince { 0 };
    // INV-AB6: the return that ended at that deadline without proof. Measurement and the entries go
    // on, but B may still carry comparison audio that the clocks and a strong content match cannot
    // tell from the ordinary path: until an ordinary marker has passed the chain and settled, nothing
    // is learnt for PRE's alignment and no correction is kept. The return observer lives on for it.
    std::atomic<std::uint64_t> tail { 0 };
    std::string tailPre; // message thread
    std::uint64_t observedReturnContext = 0; // Audio Thread only
    std::uint64_t scope = 0; // existing project capture exclusion, separate from a BLIND trial
    CandidateReturnContinuity returnContinuity; // Audio Thread only
    CandidateReturnSettling returnSettling; // Audio Thread only; no PCM authority
    std::atomic<bool> hostActive { true };
    // Audio Thread, every callback with or without a session: the tail's marker and settling count.
    void observeTail (const BlockClock& block, std::uint32_t rate, bool eligible) noexcept
    {
        const auto request = tail.load (std::memory_order_acquire);
        if (request == 0 || token.load (std::memory_order_acquire) != 0) return;
        const auto context = returnContext.load (std::memory_order_acquire);
        if (observedReturnContext != context) { returnContinuity = {}; returnSettling = {}; observedReturnContext = context; }
        bool reached = false;
        returnObserver.withRealtime ([&] (CandidateReturnObserver& observer)
        {
            auto observed = block;
            observed.clockValid = observed.clockValid && eligible;
            reached = observer.reached (observed, rate);
        });
        quietFrames.store (returnSettling.advance (request, block, rate, reached, eligible), std::memory_order_release);
        quietContext.store (context, std::memory_order_release);
    }
    bool trackMismatch = false; // message thread, guidance only
};
}
