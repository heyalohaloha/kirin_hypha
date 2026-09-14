#include "ReferenceACaptureSession.h"
#include <algorithm>
namespace hypha::reference_audition
{
void ACaptureSession::setPresented(bool value)
{
    const juce::ScopedLock lock(control); presented=value;
    if(!value && !access->active) { accepting=0; observationAdmission.reset(); access->analysisAvailable=false; }
}
void ACaptureSession::pauseObservation()
{
    const juce::ScopedLock lock(control); paused=true;
    if(!access->active) accepting=0;
    observationAdmission.reset();
    if(!access->active) access->analysisAvailable=false;
}
void ACaptureSession::resumeObservation()
{ const juce::ScopedLock lock(control); paused=false; }
void ACaptureSession::prepareObservation()
{
    const juce::ScopedLock lock(control);
    const bool revisit=state.held
        && (state.held->restored || receiver==state.held->receiver)
        && rate==state.held->rate && channels==state.held->channels;
    const bool wanted=presented && !paused && (revisit || (live && live->inputGeneration()!=0));
    if(!wanted)
    {
        accepting=0; observationAdmission.reset(); access->analysisAvailable=false;
        if(state.observationFresh) { state.observationFresh=false; publish(); }
        return;
    }
    if(!analysis->current(observationAdmission)) observationAdmission.reset();
    if(!observationAdmission) observationAdmission=analysis->acquire();
    const bool admitted=bool(observationAdmission);
    access->analysisAvailable=admitted;
    if(admitted && !accepting && !writers.load() && readIndex.load()==writeIndex.load())
    {
        kirin_reference_index_drop(captureIndex);
        captureIndex=revisit ? kirin_reference_index_create(uint32_t(rate),uint32_t(channels),uint32_t(rate)) : nullptr;
        revisitExpected=0; revisitFrames=matchingFrames=0; ++state.observationPass; accepting.store(++epoch,std::memory_order_release);
    }
}
void ACaptureSession::finishUnit()
{
    if(!draft || !unitFrames) return;
    KirinReferenceCaptureUnit unit{};
    if(kirin_reference_index_finish(captureIndex,&unit)!=unitFrames) { measurementFailed=true; terminal=3; accepting=0; }
    else draft->units.push_back(unit);
    unitFrames=0;
}
void ACaptureSession::stampReceipt(ACaptureData& data)
{
    if(!receipt || data.frames<std::uint64_t(data.rate)*4) return;
    const auto map=receipt(); const auto& proof=map.evidence;
    if(!map.verified || !map.completeProbe || !proof.valid() || proof.captureId!=data.id
        || proof.rate!=data.rate || proof.channels!=data.channels || proof.probeStart<data.hostStart
        || proof.probeEnd>data.hostStart+std::int64_t(data.frames)) return;
    for(const auto& old:data.bindings) if(old.sourceHash==proof.sourceHash) return;
    const auto offset=proof.probeStart-data.hostStart;
    if(offset%data.rate) return;
    const auto first=size_t(offset/data.rate); if(first+4>data.units.size()) return;
    for(size_t i=0;i<4;++i) if(!captureDigestEqual(data.units[first+i],map.units[i])) return;
    if(!captureUniqueSequence(data.units,first)) return;
    if(data.bindings.size()==16) data.bindings.erase(data.bindings.begin());
    data.bindings.push_back(proof); data.verifiedWork=proof.work;
}
void ACaptureSession::stampHeldReceipt()
{
    if(!state.held || ownsGate || !receipt) return;
    const auto proof=receipt();
    if(!proof.verified || proof.evidence.captureId!=state.held->id) return;
    for(const auto& old:state.held->bindings) if(old.sourceHash==proof.evidence.sourceHash) return;
    const auto key=state.held->id+":"+proof.evidence.sourceHash+":"+proof.evidence.calibrationHash+":"+juce::String(proof.evidence.probeStart);
    if(key==lastReceiptCheck) return;
    lastReceiptCheck=key;
    auto next=std::make_shared<ACaptureData>(*state.held); stampReceipt(*next);
    if(next->bindings.empty() || next->bindings.back().sourceHash!=proof.evidence.sourceHash) return;
    ++next->revision; auto encoded=encodeACapture(*next);
    if(access->store.commit(stateGeneration,next,encoded)) { state.held=std::move(next); state.encoded=std::move(encoded); publish(); }
}
void ACaptureSession::consumeRevisit(const Block& block)
{
    if(!state.held || !access->analysisAvailable || !captureIndex || block.epoch!=epoch) return;
    const auto& data=*state.held;
    const auto reset=[&] { KirinReferenceCaptureUnit ignored{}; kirin_reference_index_finish(captureIndex,&ignored); revisitFrames=matchingFrames=0; };
    if(block.continuity!=previousRevisitContinuity) { reset(); ++state.observationPass; previousRevisitContinuity=block.continuity; if(state.observationFresh) { state.observationFresh=false; publish(); } }
    if(block.channels!=data.channels || block.clock!=data.clockSource || block.config!=configuration.load() || block.signature!=data.clockSignature)
    {
        reset(); const bool changed=state.timingVerified || state.observationFresh;
        state.timingVerified=state.observationFresh=false; confirmedTimingEpoch=0;
        if(changed) publish(); return;
    }
    const bool sameRuntime=data.runtimeToken==runtimeToken && data.inputConfiguration==block.config && data.timingEpoch==block.timing;
    if(!sameRuntime && confirmedTimingEpoch!=block.timing)
    {
        if(state.observationFresh) { state.observationFresh=false; publish(); }
        state.timingVerified=false;
    }
    if(sameRuntime) { state.timingVerified=true; confirmedTimingEpoch=block.timing; state.confirmedTimingEpoch=block.timing; }
    auto at=block.position-data.hostStart;
    if(at<0 || at>=std::int64_t(data.frames)) { reset(); if(state.observationFresh) { state.observationFresh=false; publish(); } return; }
    if(block.position!=revisitExpected) { reset(); ++state.observationPass; if(state.observationFresh) { state.observationFresh=false; publish(); } }
    int offset=0;
    while(offset<block.frames && at<std::int64_t(data.frames))
    {
        const auto index=size_t(at/data.rate); const auto first=std::uint64_t(index)*std::uint64_t(data.rate);
        const auto frames=std::min(std::uint64_t(data.rate),data.frames-first);
        const auto progress=std::uint64_t(at)-first;
        const int n=int(std::min(std::uint64_t(block.frames-offset),frames-progress));
        if(index>=data.units.size()) break; // Legacy captures lack the stronger index; never invent evidence.
        if(progress==revisitFrames && kirin_reference_index_push(captureIndex,block.pcm.data()+size_t(offset*block.channels),size_t(n*block.channels)))
        {
            revisitFrames+=std::uint64_t(n);
            if(revisitFrames==frames)
            {
                KirinReferenceCaptureUnit current{};
                const bool valid=kirin_reference_index_finish(captureIndex,&current)==frames;
                const bool exact=valid && captureDigestEqual(current,data.units[index]);
                matchingFrames=exact && frames==std::uint64_t(data.rate) ? matchingFrames+1 : 0;
                if(!state.timingVerified && matchingFrames>=4 && captureUniqueSequence(data.units,index-3))
                { state.timingVerified=true; confirmedTimingEpoch=block.timing; state.confirmedTimingEpoch=block.timing; }
                if(state.unitStatus.size()!=data.units.size()) state.unitStatus.assign(data.units.size(),0);
                const auto comparison=valid ? kirin_reference_index_compare(&data.units[index],&current,uint32_t(data.channels)) : 0;
                if(state.unitPass.size()!=data.units.size()) { state.unitPass.assign(data.units.size(),0); state.unitCheckedAt.assign(data.units.size(),0); }
                state.unitPass[index]=state.observationPass; state.unitCheckedAt[index]=juce::Time::currentTimeMillis();
                state.unitStatus[index]=!valid ? 0 : exact ? 1 : (state.timingVerified && comparison==2) ? 2 : 3;
                if(state.revisited.size()!=data.bins.size()) state.revisited.assign(data.bins.size(),0);
                for(size_t b=0;b<data.bins.size();++b)
                {
                    const auto& bin=data.bins[b]; const auto end=bin.offset+bin.value.frames;
                    if(bin.offset>=first+frames || end<=first) continue;
                    bool allExact=true,changed=false;
                    for(size_t u=size_t(bin.offset/std::uint64_t(data.rate));u<state.unitStatus.size() && std::uint64_t(u)*std::uint64_t(data.rate)<end;++u)
                    { changed=changed || state.unitStatus[u]==2; allExact=allExact && state.unitStatus[u]==1; }
                    state.revisited[b]=changed ? 2 : allExact ? 1 : 0;
                }
                revisitFrames=0; state.checkedAt=juce::Time::currentTimeMillis(); state.observationFresh=state.timingVerified;
                const auto now=juce::Time::getMillisecondCounterHiRes();
                if(now>=nextRevisitPublish) { publish(); nextRevisitPublish=now+100; }
            }
        }
        else reset();
        offset+=n; at+=n;
    }
    revisitExpected=block.position+block.frames;
}
}
