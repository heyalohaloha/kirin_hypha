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
    std::uint64_t observedReturnContext = 0; // Audio Thread only
    std::uint64_t scope = 0; // existing project capture exclusion, separate from a BLIND trial
    CandidateReturnContinuity returnContinuity; // Audio Thread only
    CandidateReturnSettling returnSettling; // Audio Thread only; no PCM authority
    std::atomic<bool> hostActive { true };
    bool trackMismatch = false; // message thread, guidance only
};
}
