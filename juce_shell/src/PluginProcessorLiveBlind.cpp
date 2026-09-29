#include "PluginProcessor.h"
#include "kirin_hypha_local_blind_capture_ffi.h"
#include "reference_audition/ReferenceBlindSession.h"

#include <cmath>

using namespace hypha::live_compare;

void KirinHyphaProcessorBase::finishLiveCompare()
{
    if (role != Role::Post) return;
    ++liveCompare.blindPreparation;
    liveCompare.blind.end();
    liveCompare.blindStage = BlindStage::finishing;
    liveCompare.matched.store (false, std::memory_order_release);
    liveCompare.preSelected.store (false, std::memory_order_release);
    liveCompare.completion.request();
    startTimer (50);
}

bool KirinHyphaProcessorBase::liveCompareNeedsService() const noexcept
{
    return liveCompare.sessionActive.load (std::memory_order_acquire)
        || liveCompare.completion.pending() || liveCompare.blindScope != 0;
}

StartResult KirinHyphaProcessorBase::beginLiveBlind()
{
    using Phase = hypha::local_blind::ProductSessionPhase;
    serviceLiveCompare();
    if (! liveCompareSupported() || ! localBlindProductSupported()) return StartResult::unsupportedLayout;
    const auto local = localBlindProductView().phase;
    if (liveCompare.completion.pending() || liveCompare.blindScope != 0
        || (local != Phase::idle && local != Phase::returned && local != Phase::failed))
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
    liveCompare.blindScope = epoch;
    ++liveCompare.blindPreparation;
    liveCompare.blindMeasuredEnd = 0;
    liveCompare.blindWaiting = MatchFailure::notProven;
    liveCompare.blindStage = BlindStage::preparing;
    // The only sound during preparation is POST. The match itself can be reused unchanged.
    liveCompare.preSelected.store (false, std::memory_order_release);
    startTimer (50);
    return StartResult::started;
}

void KirinHyphaProcessorBase::serviceLiveBlind()
{
    serviceLiveCompare();
    if (liveCompare.blindStage == BlindStage::active)
    {
        if (liveCompare.blind.view().invalidated)
        {
            liveCompare.blindStage = BlindStage::invalidated;
            stopLiveCompare();
        }
        return;
    }
    if (liveCompare.blindStage != BlindStage::preparing && liveCompare.blindStage != BlindStage::settling)
        return;
    const auto status = liveCompareStatus();
    if (! status.active || status.finishing) return;
    if (status.compensationOff || status.contentHeld || status.verdict != Verdict::accepted)
    {
        liveCompare.blindWaiting = MatchFailure::notProven;
        return;
    }
    if (status.matchReady)
    {
        if (! liveCompare.blind.startWith (hypha::reference_audition::secureRandomBit))
        {
            liveCompare.blindStage = BlindStage::invalidated;
            stopLiveCompare();
            return;
        }
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
    if (liveCompare.blindPlan.needsApproval)
        liveCompare.blindStage = BlindStage::approval;
    else if (applyLiveCompareMatch (liveCompare.blindPlan, MatchChoice::basis))
        liveCompare.blindStage = BlindStage::settling;
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
    if (! applyLiveCompareMatch (liveCompare.blindPlan, MatchChoice::lowerPost))
    {
        liveCompare.blindStage = BlindStage::preparing;
        ++liveCompare.blindPreparation;
        return false;
    }
    liveCompare.blindStage = BlindStage::settling;
    return true;
}

LiveBlindStatus KirinHyphaProcessorBase::liveBlindStatus() const
{
    return { liveCompare.blindStage, liveCompare.blind.view(), liveCompare.blindWaiting,
             liveCompare.blindPlan.lowerPostGainDb, liveCompare.blindPreparation };
}

bool KirinHyphaProcessorBase::selectLiveBlind (int stimulus)
{
    return liveCompare.blindStage == BlindStage::active && liveCompare.blind.select (stimulus);
}

bool KirinHyphaProcessorBase::answerLiveBlind (int answer)
{
    return liveCompare.blindStage == BlindStage::active && liveCompare.blind.answer (answer);
}

void KirinHyphaProcessorBase::closeLiveBlind()
{
    ++liveCompare.blindPreparation;
    stopLiveCompare();
    // An explicit END already in progress survives window close. A close alone holds attenuation.
    if (! liveCompare.completion.pending()) liveCompare.blindStage = BlindStage::idle;
    startTimer (50);
}
