#include "ReferenceComparisonController.h"
namespace hypha::reference_audition
{
void ReferenceComparisonController::refreshObservation()
{
    const juce::ScopedLock lock(gateLock);
    const bool heldView=capture.access->capturedView;
    const bool observing=presented && !localBlindOwned;
    if(heldView && !trialActive()) viewedSlot.store(1,std::memory_order_release);
    version.setContentObservationEnabled(observing || captureOwned);
    const auto captured=capture.access->snapshot().shown;
    const auto selected=version.visualBinding();
    bool needsEvidence=(observing || captureOwned) && !trialActive() && captured && captured->frames && selected.source;
    if(needsEvidence) for(const auto& b:captured->bindings) if(b.sourceHash==selected.source->sourceFileSha256) needsEvidence=false;
    version.setCaptureObservation(needsEvidence ? captured->id : juce::String(),needsEvidence ? captured->hostStart : 0);
    visual.setPresented(observing && !heldView && !captureOwned);
    capture.setPresented(observing && !trialActive());
    captureProjection.setPresented(observing && !trialActive());
}
bool ReferenceComparisonController::admitCapture(bool active)
{
    const juce::ScopedLock lock(gateLock);
    if(closing) return !active;
    if(active)
    {
        if(blindGuardOwned || trialActive()) return false;
        if(!captureGate || !captureGate(true))
        { visual.resumeObservation(); capture.resumeObservation(); return false; }
        captureOwned=true;
    }
    else
    {
        const auto captured=capture.access->snapshot().shown;
        if(captured)
        {
            const juce::ScopedLock selection(selectionLock);
            tonalState.source=TonalDisplayState::Source::captured;
            tonalState.captureId=captured->id;
            tonalState.artifactSha256=captured->tonal.artifactSha256;
            tonalState.rangeStart=0;
            tonalState.rangeEnd=0;
        }
        if(captureOwned && captureGate) captureGate(false);
        captureOwned=false; visual.resumeObservation(); capture.resumeObservation();
    }
    refreshObservation(); return true;
}
bool ReferenceComparisonController::beginBlindGuard()
{
    const juce::ScopedLock lock(gateLock);
    if(captureOwned || !capture.access->reserveBlind(CaptureBlindOwner::version)) return false;
    capture.pauseObservation(); captureProjection.setPresented(false);
    if(blindCaptureGate && !blindCaptureGate(true)) { capture.access->releaseBlind(CaptureBlindOwner::version); capture.resumeObservation(); return false; }
    blindGuardOwned=true; return true;
}
void ReferenceComparisonController::endBlindGuard()
{
    const juce::ScopedLock lock(gateLock);
    if((gateOwners & 2)!=0) return; // Keep exclusion until the RT normal-return receipt retires output.
    if(blindGuardOwned && blindCaptureGate) blindCaptureGate(false);
    blindGuardOwned=false; capture.access->releaseBlind(CaptureBlindOwner::version); capture.resumeObservation(); refreshObservation();
}
bool ReferenceComparisonController::reserveLocalBlind()
{
    const juce::ScopedLock lock(gateLock);
    if(closing || !capture.access->reserveBlind(CaptureBlindOwner::local)) return false;
    localBlindOwned=true; localBlindEpoch=0;
    visual.pauseAdmission(); capture.pauseObservation(); captureProjection.setPresented(false);
    refreshObservation(); return true;
}
void ReferenceComparisonController::bindLocalBlind(std::uint64_t epoch)
{ const juce::ScopedLock lock(gateLock); if(localBlindOwned) localBlindEpoch=epoch; }
void ReferenceComparisonController::releaseLocalBlind(std::uint64_t epoch)
{
    const juce::ScopedLock lock(gateLock);
    if(!localBlindOwned || localBlindEpoch!=epoch) return;
    localBlindOwned=false; capture.access->releaseBlind(CaptureBlindOwner::local);
    visual.resumeObservation(); capture.resumeObservation(); refreshObservation();
}
ACaptureReceipt ReferenceComparisonController::captureReceipt() const
{
    const auto map=version.visualBinding();
    return map.captureEvidence && !map.hidden ? *map.captureEvidence : ACaptureReceipt{};
}
}
