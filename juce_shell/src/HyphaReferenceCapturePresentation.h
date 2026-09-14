#pragma once
#include "reference_audition/ReferenceACaptureModel.h"
namespace hypha::reference_ui
{
struct CapturePresentation
{
    juce::String primary,secondary,compactSecondary,detail,action;
    reference_audition::ACaptureAccess::Command command=reference_audition::ACaptureAccess::none;
    std::uint64_t operation=0;
    bool busy=false,cancel=false,view=false;
};
inline CapturePresentation presentCapture(const reference_audition::ACaptureState& state,std::uint64_t timing)
{
    using namespace reference_audition;
    CapturePresentation p; p.operation=state.operation.id; p.busy=state.operation.busy(); p.detail=state.message;
    switch(state.operation.phase)
    {
        case CaptureOperationPhase::starting: p.primary="PREPARING"; p.action="CANCEL"; p.command=ACaptureAccess::cancel; break;
        case CaptureOperationPhase::armed: p.primary="PLAY"; p.action="CANCEL"; p.command=ACaptureAccess::cancel; break;
        case CaptureOperationPhase::capturing: p.primary="CAPTURING"; p.action="FINISH A"; p.command=ACaptureAccess::finish; p.cancel=true; break;
        case CaptureOperationPhase::finalizing: p.primary="SAVING"; p.action="SAVING"; break;
        case CaptureOperationPhase::restoring: p.primary="RESTORING"; p.action="WAIT"; break;
        case CaptureOperationPhase::closed: p.action="CAPTURE A"; break;
        case CaptureOperationPhase::idle: p.command=ACaptureAccess::start; p.action="CAPTURE A"; break;
    }
    if(state.operation.cancellation && p.busy) { p.primary="CANCELLING"; p.action="WAIT"; p.command=ACaptureAccess::none; p.cancel=false; }
    const auto data=p.busy ? state.shown : state.held;
    if(!p.busy && data)
    {
        p.primary=data->complete ? "CAPTURED" : "PARTIAL";
        bool difference=false,current=false;
        for(size_t i=0;i<state.unitStatus.size();++i) if(state.unitStatus[i]==2)
        { difference=true; current=current || (i<state.unitPass.size() && state.unitPass[i]==state.observationPass); }
        if(difference)
        {
            const bool fresh=current && state.observationFresh && state.confirmedTimingEpoch==timing;
            p.primary=fresh ? "A DIFFERS" : "LAST: A DIFFERS";
            if(!data->complete) { p.secondary="Partial capture"; p.compactSecondary="PARTIAL"; }
        }
        p.view=true;
    }
    if(!p.busy)
    {
        juce::String failure;
        switch(state.outcome.kind)
        {
            case CaptureOutcome::unavailable: failure="Capture unavailable"; break;
            case CaptureOutcome::interrupted: failure="Capture interrupted"; break;
            case CaptureOutcome::limit: failure="Capture limit reached"; break;
            case CaptureOutcome::saveFailed: failure="Capture not saved"; break;
            case CaptureOutcome::restoreFailed: failure="Saved capture unavailable"; break;
            case CaptureOutcome::none: case CaptureOutcome::captured: case CaptureOutcome::cancelled: break;
        }
        if(failure.isNotEmpty())
        {
            if(data && state.outcome.retainedCapture==data->id) failure+=" / previous kept";
            p.compactSecondary+=(p.compactSecondary.isEmpty() ? "" : " / ")+juce::String("RETRY FAILED");
            p.secondary+=(p.secondary.isEmpty() ? "" : " / ")+failure;
            if(p.primary.isEmpty()) p.primary="CAPTURE A";
        }
    }
    if(p.secondary.isNotEmpty()) p.detail=p.secondary+(p.detail.isEmpty() ? "" : "\n"+p.detail);
    if(data) p.detail+=(p.detail.isEmpty() ? "" : "\n")+juce::String(data->duration(),1)+" s captured. Live A audio is unchanged.";
    return p;
}
}
