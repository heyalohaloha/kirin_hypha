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
    std::uint64_t observedReturnContext = 0; // Audio Thread only
    std::uint64_t scope = 0; // existing project capture exclusion, separate from a BLIND trial
    CandidateReturnContinuity returnContinuity; // Audio Thread only
    CandidateReturnSettling returnSettling; // Audio Thread only; no PCM authority
    std::atomic<bool> hostActive { true };
    bool trackMismatch = false; // message thread, guidance only
};
}
