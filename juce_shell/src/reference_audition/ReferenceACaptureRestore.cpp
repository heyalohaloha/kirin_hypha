#include "ReferenceACaptureSession.h"
namespace hypha::reference_audition
{
void ACaptureSession::serviceRestore()
{
    juce::String encoded; bool show=true; std::uint64_t token=0;
    {
        const juce::ScopedLock lock(control);
        if(!restorePending) return;
        encoded=std::move(restoreText); show=restoreShown; token=restoreGeneration; restorePending=false;
    }
    if(!access->store.isCurrent(token)) return;
    pauseObservation(); accepting=0;
    while(writers.load(std::memory_order_acquire)) juce::Thread::yield();
    readIndex=writeIndex.load(std::memory_order_acquire);
    if(ownsGate) close(5);
    const auto decoded=decodeACapture(encoded);
    if(access->store.finishRestore(token,decoded))
    {
        stateGeneration=token; state={};
        const auto saved=access->store.value(); state.held=saved.document; state.encoded=saved.encoded;
        state.phase=!state.held ? ACapturePhase::idle : state.held->complete ? ACapturePhase::held : ACapturePhase::partial;
        if(!encoded.isEmpty() && !decoded) state.message="Saved capture unavailable / previous capture kept";
        else if(state.held && !state.held->complete) state.message="Partial capture / capture again";
        if(state.held) { state.revisited.assign(state.held->bins.size(),0); state.unitStatus.assign(state.held->units.size(),0); }
        confirmedTimingEpoch=0;
        access->presentIfCurrent(token,bool(state.held) && show);
        publish();
    }
    { const juce::ScopedLock lock(control); paused=false; }
}
}
