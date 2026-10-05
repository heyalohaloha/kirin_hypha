#pragma once

#include "../local_blind/RtPublicationSlot.h"
#include "LiveCompareAaxGroup.h"
#include "LiveCompareClock.h"
#include "LiveCompareMatch.h"
#include "LiveCompareOffset.h"
#include "LiveComparePinResult.h"
#include "LiveCompareSession.h"
#include "LiveCompareSharedRing.h"
#include "LiveCompareCompletion.h"
#include "LiveBlindSession.h"
#include "LiveCompareAuthority.h"
#include "LiveCompareTimingPreparation.h"

#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
enum class BlindStage { idle, preparing, approval, settling, active, invalidated, finishing, failed };
struct LiveBlindStatus
{
    BlindStage stage = BlindStage::idle;
    BlindView trial;
    MatchFailure waiting = MatchFailure::notProven;
    double lowerPostDb = 0.0;
    std::uint64_t generation = 0;
    RecoveryReason reason = RecoveryReason::none;
    RecoveryReason observation = RecoveryReason::none;
    bool contentHeld = false, compensationOff = false;
};

struct Status
{
    bool finishing = false, matched = false, matchLimited = false, matchReady = false, matchHeld = false;
    float postActual = 1.0f;
    std::uint64_t sessionGeneration = 0;
    bool active = false;
    bool preSelected = false;
    bool preAudible = false;
    bool preWaiting = false;  // PRE selected, POST sounding because correspondence is not proven
    bool interrupted = false; // offline render or bypass ended the session; select PRE again
    Verdict verdict = Verdict::noClock;
    float gain = 1.0f;
    float postTarget = 1.0f;  // approved POST attenuation, held after the session until RETURN
    bool contentHeld = false; // INV-LC10: POST until playback stops and restarts
    std::int64_t contentJumpLagFrames = 0; // settled offset that caused the hold; zero when not held
    bool compensationOff = false; // INV-LC8: the host's delay compensation is off
    RecoveryReason reason = RecoveryReason::none; // retained first failure, never a current blocker
    RecoveryReason observation = RecoveryReason::none;
};

// Everything one processor owns for the live compare. PRE uses the ring and the feeder; POST uses
// the ring and the renderer. The message thread publishes and retires the mapping; the Audio
// Thread reads it only through the slot and publishes its observations through the atomics.
struct ProcessorState
{
    Authority authority;
    std::uint64_t restoreServiced = 0; // message thread retires the revoked session
    Completion completion;
    BlindSession blind;
    NamedSelection selection;
    std::atomic<std::uint64_t> sessionGeneration { 0 }, gainRevision { 0 }, gainReceipt { 0 };
    std::atomic<bool> matched { false }, matchLimited { false };
    std::atomic<bool> matchRetained { false }; // fixed gains remain, independently of current proof
    std::atomic<std::uint32_t> matchRun { 0 };
    std::atomic<float> postActual { 1.0f };
    BlindStage blindStage = BlindStage::idle; // message thread only
    RecoveryReason blindPreparationReason = RecoveryReason::none;
    MatchFailure blindWaiting = MatchFailure::notProven;
    MatchPlan blindPlan;
    std::uint64_t blindPreparation = 0, blindScope = 0, finishServiced = 0;
    std::int64_t blindMeasuredEnd = 0;
    std::uint32_t blindApprovalRun = 0;
    std::atomic<std::uint64_t> timelineGeneration { 0 };
    std::atomic<std::uint64_t> matchGeneration { 0 };
    std::uint64_t blindApprovalTimeline = 0;
    local_blind::RtPublicationSlot<SharedRingMapping> ring;
    PreFeeder feeder;
    PostRenderer renderer;
    ContinuousClock clock;
    GapDetector gaps;
    std::uint8_t clockAuthority = 0;       // prepareToPlay, host suspends Audio Thread
    std::uint32_t maximumDelaySamples = 0; // certified host bound, never a measured guess
    TimingPreparation preparation;
    ChainTimingMeter chain; // POST, display only: the host work between PRE's callback and its own
    std::atomic<bool> sessionActive { false }; // user session, independent of the ring's fade/ramp lease
    std::atomic<bool> preAudible { false };
    std::atomic<bool> preWaiting { false };
    std::atomic<bool> preWaitSeen { false }; // any waiting block since the editor last looked
    std::atomic<float> gain { 1.0f };
    std::atomic<std::uint8_t> verdict { 0 };
    std::atomic<RecoveryReason> observationReason { RecoveryReason::none };
    PostLevel postLevel;                      // Audio Thread; the message thread configures it
    std::atomic<float> postTarget { 1.0f };   // approved POST attenuation (linear, at most 1)
    std::atomic<float> ceilingLinear { 1.0f }; // PRE guard fixed at MATCH, 10^(C/20)
    std::atomic<bool> guardTripped { false };
    std::atomic<bool> contentHold { false };      // INV-LC10: the content offset jumped
    std::atomic<std::int64_t> contentJumpLagFrames { 0 }; // published before contentHold
    std::atomic<std::uint32_t> playbackRun { 0 }; // counts stop-to-play transitions
    bool wasPlaying = false;                      // Audio Thread only
    std::atomic<bool> compensationOff { false };  // INV-LC8: the host says delay compensation is off
    bool compensationWasOff = false;              // Audio Thread only
    AaxGroupMembership aaxGroup;                  // INV-LC9: the host's AAX instance group
};
}
