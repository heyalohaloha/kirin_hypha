#include "PluginProcessor.h"
#include "kirin_hypha_local_blind_capture_ffi.h"
#include "reference_audition/ReferenceBlindSession.h"

#include <cmath>

using namespace hypha::live_compare;

void KirinHyphaProcessorBase::finishLiveCompare()
{
    if (role != Role::Post) return;
    // End logical ownership now. The published mapping remains leased for the RT fade/ramp.
    liveCompare.sessionActive.store (false, std::memory_order_release);
    liveCompare.blindTiming.end();
    liveCompare.cancelTimingAdmission();
    ++liveCompare.blindPreparation;
    liveCompare.blind.end();
    liveCompare.blindStage = BlindStage::finishing;
    liveCompare.matched.store (false, std::memory_order_release);
    liveCompare.matchRetained.store (false, std::memory_order_release);
    liveCompare.selection.end();
    liveCompare.completion.request();
    startTimer (50);
}

bool KirinHyphaProcessorBase::liveCompareNeedsService() const noexcept
{
    return liveCompare.sessionActive.load (std::memory_order_acquire)
        || (role == Role::Post && writesEnabled.load (std::memory_order_acquire) && liveCompareSupported())
        || liveCompare.authority.restoring() || liveCompare.authority.generation() != liveCompare.restoreServiced
        || liveCompare.completion.command() != liveCompare.finishServiced || liveCompare.blindScope != 0;
}

StartResult KirinHyphaProcessorBase::beginLiveBlind()
{
    using Phase = hypha::local_blind::ProductSessionPhase;
    const auto permission = liveCompare.authority.ticket();
    serviceLiveCompare();
    if (! liveCompareSupported() || ! localBlindProductSupported()) return StartResult::unsupportedLayout;
    const auto admission = liveCompareAdmission (true);
    if (admission != StartResult::started) return admission;
    const auto local = localBlindProductView().phase;
    if (local != Phase::idle && local != Phase::returned && local != Phase::failed)
        return StartResult::comparisonBusy;
    const bool startedHere = ! liveCompare.sessionActive.load (std::memory_order_acquire);
    if (startedHere)
    {
        const auto result = startLiveCompare();
        if (result != StartResult::started) return result;
    }
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController && ! referenceAuditionController->reserveLocalBlind())
    {
        if (startedHere) stopLiveCompare();
        return StartResult::comparisonBusy;
    }
   #endif
    std::uint64_t epoch = 0;
    bool admitted;
    {
        const juce::ScopedLock lock (handleLock);
        admitted = hyphaHandle && kirin_hypha_begin_local_blind (hyphaHandle, &epoch);
    }
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController)
    {
        if (admitted) referenceAuditionController->bindLocalBlind (epoch);
        else referenceAuditionController->releaseLocalBlind (0);
    }
   #endif
    if (! admitted)
    {
        if (startedHere) stopLiveCompare();
        return StartResult::comparisonBusy;
    }
    if (liveCompare.authority.ticket() != permission || ! liveCompare.authority.permitted())
    {
        releaseLocalBlindProductScope (epoch);
        if (startedHere) stopLiveCompare();
        return StartResult::notReady;
    }
    liveCompare.blindScope = epoch;
    liveCompare.blind.reset();
    liveCompare.blindPreparationReason = RecoveryReason::none;
    ++liveCompare.blindPreparation;
    liveCompare.blindMeasuredEnd = 0;
    liveCompare.blindWaiting = MatchFailure::notProven;
    liveCompare.blindStage = BlindStage::preparing;
    // Every explicit trial owns a new timing admission. A current named MATCH tuple is kept,
    // then re-certified by the first fresh RT proof; old renderer/UI proof never seeds this epoch.
    liveCompare.matched.store (false, std::memory_order_release);
    liveCompare.sessionGeneration.fetch_add (1, std::memory_order_acq_rel);
    const auto requiredAdmission = liveCompare.blindTimingRequest.load (std::memory_order_relaxed) + 1;
    liveCompare.blindTiming.begin (requiredAdmission);
    liveCompare.blindTimingAuthority.store (permission, std::memory_order_relaxed);
    liveCompare.blindTimingRequest.fetch_add (1, std::memory_order_release);
    // The only sound during preparation is POST. The match itself can be reused unchanged.
    liveCompare.selection.select (false);
    startTimer (50);
    return StartResult::started;
}

void KirinHyphaProcessorBase::serviceLiveBlind()
{
    serviceLiveCompare();
    const auto preparingLoss = liveCompare.blindTiming.failure();
    if (preparingLoss != RecoveryReason::none
        && (liveCompare.blindStage == BlindStage::preparing || liveCompare.blindStage == BlindStage::settling
            || liveCompare.blindStage == BlindStage::approval))
    {
        liveCompare.blindPreparationReason = preparingLoss;
        liveCompare.blindStage = BlindStage::invalidated;
        stopLiveCompare (preparingLoss);
        return;
    }
    if (liveCompare.blindStage == BlindStage::active)
    {
        if (liveCompare.blind.view().invalidated)
        {
            liveCompare.blindStage = BlindStage::invalidated;
            stopLiveCompare (RecoveryReason::unknown);
        }
        return;
    }
    if (liveCompare.blindStage != BlindStage::preparing && liveCompare.blindStage != BlindStage::settling)
        return;
    const auto status = liveCompareStatus();
    if (! status.active || status.finishing) return;
    if (liveCompare.blindTimingReceipt.load (std::memory_order_acquire)
        != liveCompare.blindTimingRequest.load (std::memory_order_acquire)) return;
    if (status.compensationOff || status.contentHeld || status.verdict != Verdict::accepted)
    {
        liveCompare.blindWaiting = MatchFailure::notProven;
        return;
    }
    if (status.matchReady)
    {
        const auto gains = readGainSnapshot (liveCompare);
        if (! currentGainReceipt (liveCompare, gains)) return;
        liveCompare.blindGainRevision.store (gains.revision, std::memory_order_release);
        if (! liveCompare.blind.startWith (hypha::reference_audition::secureRandomBit))
        {
            liveCompare.blindStage = BlindStage::invalidated;
            stopLiveCompare (RecoveryReason::randomUnavailable);
            return;
        }
        if (! liveCompare.authority.permitted() || ! currentGainReceipt (liveCompare, gains)
            || liveCompare.blindTiming.failure() != RecoveryReason::none)
        { liveCompare.blind.end(); return; }
        liveCompare.blindTiming.end();
        liveCompare.blindStage = BlindStage::active;
        return;
    }
    if (status.matched && ! status.matchLimited) return; // the RT gain ramp has not settled
    const auto history = liveCompare.renderer.historyView();
    // Retry on fresh half-second windows only; silence never spins the analyser at UI tick rate.
    const auto stride = static_cast<std::int64_t> (preparedFormat.sampleRate * 0.5);
    if (history.end - liveCompare.blindMeasuredEnd < stride) return;
    liveCompare.blindMeasuredEnd = history.end;
    const auto result = measureLiveCompare();
    liveCompare.blindWaiting = result.failure;
    if (! result.ok()) return;
    const auto held = status.postTarget > 0.0f ? 20.0 * std::log10 (status.postTarget) : 0.0;
    liveCompare.blindPlan = planMatch (result, held);
    liveCompare.blindApprovalRun = liveComparePlaybackRun();
    liveCompare.blindApprovalTimeline = liveCompare.timelineGeneration.load (std::memory_order_acquire);
    if (liveCompare.blindPlan.failure != MatchFailure::none)
    {
        liveCompare.blindWaiting = liveCompare.blindPlan.failure;
        stopLiveCompare();
        liveCompare.blindStage = BlindStage::failed;
    }
    else if (liveCompare.blindPlan.needsApproval)
        liveCompare.blindStage = BlindStage::approval;
    else
    {
        const auto applied = applyLiveCompareMatch (liveCompare.blindPlan, MatchChoice::basis);
        liveCompare.blindWaiting = applied.failure;
        if (applied) liveCompare.blindStage = BlindStage::settling;
        else if (applied.failure != MatchFailure::stale)
        {
            stopLiveCompare();
            liveCompare.blindStage = BlindStage::failed;
        }
    }
}

bool KirinHyphaProcessorBase::approveLiveBlindMatch (std::uint64_t generation)
{
    serviceLiveCompare();
    if (liveCompare.blindStage != BlindStage::approval || generation != liveCompare.blindPreparation)
        return false;
    if (liveCompare.blindApprovalRun != liveComparePlaybackRun()
        || liveCompare.blindApprovalTimeline != liveCompare.timelineGeneration.load (std::memory_order_acquire))
    {
        liveCompare.blindStage = BlindStage::preparing;
        ++liveCompare.blindPreparation;
        return false;
    }
    const auto applied = applyLiveCompareMatch (liveCompare.blindPlan, MatchChoice::lowerPost);
    if (! applied)
    {
        liveCompare.blindWaiting = applied.failure;
        if (applied.failure == MatchFailure::stale) liveCompare.blindStage = BlindStage::preparing;
        else
        {
            stopLiveCompare();
            liveCompare.blindStage = BlindStage::failed;
        }
        ++liveCompare.blindPreparation;
        return false;
    }
    liveCompare.blindStage = BlindStage::settling;
    return true;
}

LiveBlindStatus KirinHyphaProcessorBase::liveBlindStatus() const
{
    LiveBlindStatus status { liveCompare.blindStage, liveCompare.blind.view(), liveCompare.blindWaiting,
                            liveCompare.blindPlan.lowerPostGainDb, liveCompare.blindPreparation };
    const auto state = liveCompareStatus();
    status.reason = state.reason;
    status.observation = state.observation;
    status.contentHeld = state.contentHeld;
    status.compensationOff = state.compensationOff;
    if ((status.stage == BlindStage::preparing || status.stage == BlindStage::settling || status.stage == BlindStage::approval)
        && liveCompare.blindTiming.failure() != RecoveryReason::none)
    {
        status.stage = BlindStage::invalidated;
        status.reason = liveCompare.blindTiming.failure();
        status.trial = {};
    }
    if (! liveCompare.authority.permitted() && status.stage != BlindStage::idle)
    {
        status.trial = {}; // no stale assignment, played receipts or answer while service is pending
        if (status.reason == RecoveryReason::none) status.reason = RecoveryReason::restored;
        if (status.stage != BlindStage::finishing) status.stage = BlindStage::invalidated;
    }
    else if (status.stage == BlindStage::active
        && ! currentBlindReceipt (liveCompare))
    {
        status.trial = {};
        status.stage = BlindStage::invalidated;
        if (status.reason == RecoveryReason::none) status.reason = state.observation != RecoveryReason::none
            ? state.observation : RecoveryReason::gainChanged;
    }
    return status;
}

bool KirinHyphaProcessorBase::selectLiveBlind (int stimulus)
{
    return liveCompare.authority.permitted() && liveCompare.blindStage == BlindStage::active
        && currentBlindReceipt (liveCompare)
        && liveCompare.blind.select (stimulus);
}

bool KirinHyphaProcessorBase::revealLiveBlind()
{
    return liveCompare.authority.permitted() && liveCompare.blindStage == BlindStage::active
        && currentBlindReceipt (liveCompare)
        && liveCompare.blind.reveal();
}

void KirinHyphaProcessorBase::closeLiveBlind()
{
    ++liveCompare.blindPreparation;
    stopLiveCompare();
    // An explicit END already in progress survives window close. A close alone holds attenuation.
    if (! liveCompare.completion.pending()) liveCompare.blindStage = BlindStage::idle;
    startTimer (50);
}
