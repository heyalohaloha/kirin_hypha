#pragma once

#include "../local_blind/RtPublicationSlot.h"
#include "AaxChunkPosition.h"
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
#include "CandidateProcessorState.h"
#include "LiveCompareGainApproval.h"
#include "LiveBlindPreparation.h"
#include "LiveCompareContentHold.h"
#include "LiveCompareWaitNotice.h"
#include "CertifiedProjectAnalysisClock.h"
#include "LiveCompareLoopGuidance.h"
#include "RecordLoopWrapWitness.h"
#include "ReaperChainStatus.h"
#include "../chain_trace/ChainTraceWriter.h"

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
    std::int64_t contentLagFrames = 0; // INV-LC10 notice: positive when PRE is late
    double contentNoticeMs = 0.0;      // the last notice in this trial, for the RESULT only
    LoopPairingBlock loopBlock = LoopPairingBlock::none;
    ReaperChainRestriction reaperRestriction = ReaperChainRestriction::none;
    bool prolongedWait = false, signalWait = false;
    bool candidate = false, candidateBypassed = false, candidateHostInactive = false;
    int matchSecondsRemaining = 0;
    bool clockUnavailable = false;  // the host reports neither a continuous clock nor output latency: PRE cannot be heard
};

struct Status
{
    bool finishing = false, matched = false, matchLimited = false, matchReady = false, matchHeld = false;
    bool matchOff = false; // MATCH turned off in this LISTEN; MATCH turns the same match on again
    float postActual = 1.0f;
    std::uint64_t sessionGeneration = 0;
    bool active = false;
    bool preSelected = false;
    bool preAudible = false;
    bool preWaiting = false;  // PRE selected, POST sounding because correspondence is not proven
    bool interrupted = false; // offline render or bypass ended the session; select PRE again
    bool timingReentryPending = false;
    bool timingUnavailable = false;
    LoopPairingBlock loopBlock = LoopPairingBlock::none;
    ReaperChainRestriction reaperRestriction = ReaperChainRestriction::none;
    // The audio clock is missing and this host has reported neither a continuous clock nor its
    // output latency since this instance began: PRE cannot be heard here, and stop/play will not help.
    bool prolongedWait = false, signalWait = false;
    bool clockUnavailable = false;
    Verdict verdict = Verdict::noClock;
    float gain = 1.0f;
    float postTarget = 1.0f;  // approved POST attenuation, held after the session until RETURN
    bool contentHeld = false; // INV-LC10: a strong estimate the clock cannot act on; PRE still sounds at K
    std::int64_t contentJumpLagFrames = 0; // the settled offset of that notice; zero without one
    bool compensationOff = false; // INV-LC8: the host's delay compensation is off
    RecoveryReason reason = RecoveryReason::none; // retained first failure, never a current blocker
    RecoveryReason observation = RecoveryReason::none;
};

// Everything one processor owns for the live compare. PRE uses the ring and the feeder; POST uses
// the ring and the renderer. The message thread publishes and retires the mapping; the Audio
// Thread reads it only through the slot and publishes its observations through the atomics.
struct ProcessorState
{
    std::unique_ptr<chain_trace::Recorder> trace = chain_trace::Recorder::fromEnvironment();
    Authority authority;
    CandidateProcessorState candidate;
    std::uint64_t restoreServiced = 0; // message thread retires the revoked session
    std::atomic<std::uint64_t> pairRevocationGeneration { 0 };
    Completion completion;
    // INV-LC14: the last POST callback's wall time, and each return the message thread finished in a quiet.
    std::atomic<std::uint64_t> lastCallbackNanos { 0 }, quietReturn { 0 };
    std::uint64_t quietReturnSeen = 0; // Audio Thread only
    WaitNotice waitNotice;
    BlindSession blind;
    BlindPreparation blindTiming;
    NamedSelection selection;
    NamedReentry reentry; // Audio Thread only; no output/Blind authority
    GainApproval gainApproval; // all fields covered by gainRevision
    std::atomic<std::uint64_t> sessionGeneration { 0 }, gainRevision { 0 }, gainReceipt { 0 };
    std::atomic<std::uint64_t> blindGainRevision { 0 }; // trial's approved tuple, not a UI inference
    // Audio Thread only: the ratio a BLIND trial (epoch) took on the gain revision it met the ceiling
    // with (PRE back at unity, POST lowered as much), until the audio thread commits it as a new
    // revision. `post` is that lowered POST target: a gain writer landing first never raises POST.
    struct BlindLevel
    {
        std::uint32_t epoch = 0;
        std::uint64_t revision = 0;
        float shift = 1.0f, post = 1.0f;
        bool pending() const noexcept { return shift < 1.0f; }
    } blindLevel;
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
    // MATCH on/off: what MATCH off kept for MATCH on, and POST's level when this LISTEN began (message
    // thread only). A new MATCH, END and a new LISTEN forget it.
    struct MatchMemory
    {
        float pre = 1.0f, post = 1.0f, ceiling = 1.0f;
        bool limited = false, matched = false;
        std::uint32_t run = 0;
        std::uint64_t generation = 0;
        GainIdentity identity;
    } matchMemory;
    float listenBasePost = 1.0f;
    std::atomic<bool> matchOff { false };
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
    PairedAnalysisClock analysisClock;
    CertifiedProjectAnalysisClock projectAnalysisClock;
    PostRenderer renderer;
    ContinuousClock clock;
    AaxChunkPosition chunkPosition; // Audio Thread only
    std::atomic<std::uint64_t> formatGeneration { 0 }; // each prepare; a chunk proof ends with its format
    std::uint64_t formatSeen = 0;                      // Audio Thread only
    GapDetector gaps;
    ReaperCallbackContinuity reaperContinuity;
    std::uint8_t clockAuthority = 0;       // prepareToPlay, host suspends Audio Thread
    std::uint32_t maximumDelaySamples = 0; // certified host bound, never a measured guess
    TimingPreparation preparation;
    ChainTimingMeter chain; // POST, display only: the host work between PRE's callback and its own
    std::atomic<bool> sessionActive { false }; // user session, independent of the ring's fade/ramp lease
    std::atomic<std::uint64_t> listenSession { 0 }; // moves only when LISTEN starts or ends, never on a loop or a gap
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
    // Message-thread request survives editor closure; output authority is never serialized.
    bool namedMatchPending = false, namedMatchReady = false, namedMatchApplied = false;
    std::uint64_t namedMatchNext = 0, namedMatchAuthority = 0, namedMatchSince = 0, blindMatchSince = 0;
    MatchFailure namedMatchWaiting = MatchFailure::none;
    MatchPlan namedMatchPlan;
    OffsetMonitor contentMonitor;
    OffsetMonitor::Step contentStep;
    std::uint32_t contentMonitorRun = 0;
    std::uint64_t contentMonitorNext = 0, contentAlignRevision = 0;
    std::uint64_t alignConfirmStart = 0;          // message thread: the short wait for content (INV-LC28)
    std::uint64_t holdEvidenceAt = 0;             // message thread: the last confident window for a notice (INV-LC10)
    double blindContentNoticeMs = 0.0;            // message thread: the last notice since this BLIND began
    std::atomic<bool> candidateUnconfirmed { false }; // a real window confirmed no position for A
    OffsetWindow contentWindow;
    ContentHold contentHold;      // INV-LC10: a status-line notice; never changes the output
    std::atomic<std::int64_t> contentJumpLagFrames { 0 }; // published before contentHold
    std::atomic<std::uint32_t> playbackRun { 0 }; // counts stop-to-play transitions
    OffsetEstimate offsetFacts, contentJumpFacts; // message thread, diagnostic only; never saved
    std::atomic<bool> autoAlign { true };         // INV-LC28; saved only when off
    std::atomic<std::uint64_t> alignFormat { 0 }, alignPreference { 0 }, alignPeer { 0 };
    AlignmentConditions alignmentConditions; // Audio Thread only
    AlignContext alignServiced;                  // message thread: the monitor's pair and format
    std::uint64_t alignPreferenceSeen = 0;
    AlignContext alignContext() const noexcept
    { const auto* mapping = ring.control(); return alignContext (mapping != nullptr ? mapping->ring() : nullptr); }
    AlignContext alignContext (const Ring* source) const noexcept
    {
        const AlignContext context { authority.ticket(), alignFormat.load (std::memory_order_acquire) };
        return source != nullptr ? alignOwner (context, *source) : context;
    }
    std::atomic<std::uint64_t> hostResetGeneration { 0 };
    std::uint64_t hostResetSeen = 0, stopSeen = 1; // Audio Thread only
    bool wasPlaying = false;                      // Audio Thread only
    std::atomic<bool> compensationOff { false };  // INV-LC8: the host says delay compensation is off
    std::atomic<std::uint64_t> compensationRevision { 0 };
    std::uint64_t compensationSeen = 0;
    bool compensationWasOff = false;              // Audio Thread only
    // Shared callback result for analysis, LISTEN and BLIND; never persisted.
    std::atomic<LoopPairingBlock> loopBlock { LoopPairingBlock::none };
    RecordLoopWrapWitness keepLoopWrap; // Audio Thread: a captured callback begins the host's LOOP wrap
    // Host capability, for guidance only and never a proof: whether any callback of this instance
    // carried a continuous clock, and whether the host ever reported its output presentation
    // latency (the read path in PluginProcessorHostClock.cpp). A host that supplies neither
    // cannot verify PRE timing, so a missing clock there is not a transient to restart out of.
    std::atomic<bool> clockSeen { false }, outputLatencySeen { false };
    AaxGroupMembership aaxGroup;                  // INV-LC9: the host's AAX instance group
    // INV-LC16: AUTO rests while no window shows it (message thread only); the same session continues it.
    // The rest is bound to the LISTEN (listenSession) and to the restore and pair ticket, not to sessionGeneration.
    struct AutoRest { bool on = false; double approvedPreDb = 0.0, ceilingDbtp = 0.0; std::uint64_t session = 0, ticket = 0; double peakDbtp = 0.0; bool matchKnown = false; } autoRest;
    bool realtimeBounceNoticeGiven = false;  // message thread only: said once per plug-in (realtimeBounceNotice)
};
}
