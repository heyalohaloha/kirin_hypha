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
#include "LiveCompareReentry.h"
#include "LiveCompareGainApproval.h"
#include "LiveBlindPreparation.h"
#include "LiveCompareContentHold.h"

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
    bool contentHeld = false, compensationOff = false, timingUnavailable = false;
    bool loopBlocksPairing = false; // an uncertified AAX host loops a range: no K until loop playback is off
    bool clockUnavailable = false;  // the host reports neither a continuous clock nor output latency: PRE cannot be heard
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
    bool timingReentryPending = false;
    bool timingUnavailable = false;
    bool loopBlocksPairing = false; // an uncertified AAX host loops a range: turn loop playback off
    // The audio clock is missing and this host has reported neither a continuous clock nor its
    // output latency since this instance began: PRE cannot be heard here, and stop/play will not help.
    bool clockUnavailable = false;
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
    std::atomic<std::uint64_t> pairRevocationGeneration { 0 };
    Completion completion;
    BlindSession blind;
    BlindPreparation blindTiming;
    NamedSelection selection;
    NamedReentry reentry; // Audio Thread only; no output/Blind authority
    GainApproval gainApproval; // all fields covered by gainRevision
    std::atomic<std::uint64_t> sessionGeneration { 0 }, gainRevision { 0 }, gainReceipt { 0 };
    std::atomic<std::uint64_t> blindGainRevision { 0 }; // trial's approved tuple, not a UI inference
    std::atomic<std::uint64_t> blindTimingRequest { 0 }, blindTimingAuthority { 0 };
    std::atomic<std::uint64_t> blindTimingReceipt { 0 };
    std::uint64_t blindTimingSeen = 0; // Audio Thread only
    void cancelTimingAdmission() noexcept // message thread; old pending requests cannot be adopted
    {
        blindTimingAuthority.store (UINT64_MAX, std::memory_order_relaxed); // never a permitted ticket
        blindTimingRequest.fetch_add (1, std::memory_order_release);
    }
    std::atomic<bool> timingReentryPending { false };
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
    AaxAnalysisClock analysisClock;
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
    ContentHold contentHold;      // INV-LC10: the content offset jumped
    std::atomic<std::int64_t> contentJumpLagFrames { 0 }; // published before contentHold
    std::atomic<std::uint32_t> playbackRun { 0 }; // counts stop-to-play transitions
    OffsetEstimate offsetFacts, contentJumpFacts; // message thread, diagnostic only; never saved
    std::atomic<std::uint64_t> hostResetGeneration { 0 };
    std::uint64_t hostResetSeen = 0, stopSeen = 1; // Audio Thread only
    bool wasPlaying = false;                      // Audio Thread only
    std::atomic<bool> compensationOff { false };  // INV-LC8: the host says delay compensation is off
    bool compensationWasOff = false;              // Audio Thread only
    // AAX POST: the host loops a range and carries no certificate, so no pair proof can start.
    // Published for guidance only; it never grants or revokes a proof.
    std::atomic<bool> loopBlocksPairing { false };
    // Host capability, for guidance only and never a proof: whether any callback of this instance
    // carried a continuous clock, and whether the host ever reported its output presentation
    // latency (the read path in PluginProcessorHostClock.cpp). A host that supplies neither
    // cannot verify PRE timing, so a missing clock there is not a transient to restart out of.
    std::atomic<bool> clockSeen { false }, outputLatencySeen { false };
    AaxGroupMembership aaxGroup;                  // INV-LC9: the host's AAX instance group
};
}
