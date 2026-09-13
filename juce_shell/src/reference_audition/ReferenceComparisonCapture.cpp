#include "ReferenceComparisonController.h"
namespace hypha::reference_audition
{
void ReferenceComparisonController::refreshObservation()
{
    const juce::ScopedLock lock(gateLock);
    const bool heldView=capture.access->capturedView;
    if(heldView && !trialActive()) viewedSlot.store(1,std::memory_order_release);
    version.setContentObservationEnabled(presented || captureOwned);
    visual.setPresented(presented && !heldView && !captureOwned);
    capture.setPresented(presented && !trialActive());
    captureProjection.setPresented(presented && !trialActive());
}
bool ReferenceComparisonController::admitCapture(bool active)
{
    const juce::ScopedLock lock(gateLock);
    if(closing) return !active;
    if(active)
    {
        if(blindGuardOwned || trialActive()) return false;
        visual.pauseAdmission(); capture.pauseObservation();
        if(!captureGate || !captureGate(true))
        { visual.useAuditionAdmission(gateOwners!=0); capture.useAuditionAdmission(gateOwners!=0); return false; }
        captureOwned=true;
    }
    else
    {
        if(captureOwned && captureGate) captureGate(false);
        captureOwned=false; visual.useAuditionAdmission(gateOwners!=0); capture.useAuditionAdmission(gateOwners!=0);
    }
    refreshObservation(); return true;
}
bool ReferenceComparisonController::beginBlindGuard()
{
    const juce::ScopedLock lock(gateLock);
    if(captureOwned || capture.access->active || capture.access->pending==ACaptureAccess::start) return false;
    capture.pauseObservation(); captureProjection.setPresented(false);
    if(blindCaptureGate && !blindCaptureGate(true)) { capture.useAuditionAdmission(gateOwners!=0); return false; }
    blindGuardOwned=true; return true;
}
void ReferenceComparisonController::endBlindGuard()
{
    const juce::ScopedLock lock(gateLock);
    if((gateOwners & 2)!=0) return; // Keep exclusion until the RT normal-return receipt retires output.
    if(blindGuardOwned && blindCaptureGate) blindCaptureGate(false);
    blindGuardOwned=false; capture.useAuditionAdmission(gateOwners!=0); refreshObservation();
}
ACaptureReceipt ReferenceComparisonController::captureReceipt() const
{
    const auto map=version.visualBinding(); ACaptureReceipt result;
    result.verified=map.aligned && !map.hidden && map.source && map.hostPositionValid;
    if(result.verified) result.work=map.source->sourceIdentityKey.upToFirstOccurrenceOf(":",false,false);
    result.rate=int(map.hostRate); result.hostPosition=map.hostPosition; return result;
}
}
