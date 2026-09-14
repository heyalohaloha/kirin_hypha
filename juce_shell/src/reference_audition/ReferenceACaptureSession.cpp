#include "ReferenceACaptureSession.h"
#include <cmath>
namespace hypha::reference_audition
{
ACaptureSession::ACaptureSession(std::function<bool(bool)> f,std::function<ACaptureReceipt()> proof)
    :juce::Thread("Capture A"),gate(std::move(f)),receipt(std::move(proof)),observationAdmission(kirin_reference_visual_admission_create())
{ startThread(juce::Thread::Priority::low); }
ACaptureSession::~ACaptureSession() { shutdown(); }
void ACaptureSession::shutdown()
{
    access->alive=false; accepting=0; signalThreadShouldExit(); access->wake.signal(); notify(); stopThread(-1);
    while(writers.load(std::memory_order_acquire)) juce::Thread::yield();
    if(ownsGate && gate) gate(false); ownsGate=false; access->active=false;
    kirin_reference_visual_drop(meter); meter=nullptr;
    kirin_reference_index_drop(captureIndex); captureIndex=nullptr;
    kirin_reference_visual_admission_drop(observationAdmission); observationAdmission=nullptr;
}
void ACaptureSession::configure(juce::String id,double sr,int ch)
{
    const juce::ScopedLock lock(control);
    const int next=std::isfinite(sr) && sr>=8000 && sr<=768000 ? int(std::llround(sr)) : 0;
    if(receiver==id && rate==next && channels==ch) return;
    receiver=std::move(id); rate=next; channels=ch; ++configuration;
}
void ACaptureSession::restore(juce::String value,bool shown)
{
    const juce::ScopedLock lock(control);
    restoreGeneration=access->beginRestore(value,shown);
    restoreText=std::move(value); restoreShown=shown; restorePending=true;
    access->wake.signal();
}
bool ACaptureSession::observe(const juce::AudioBuffer<float>& input,std::int64_t position,bool valid,bool playing,bool allowed,int clock,CaptureClockSignature signature) noexcept
{
    if(clock!=rtClock || signature!=rtSignature || !valid || !allowed)
    { ++rtTimingEpoch; rtClock=clock; rtSignature=signature; }
    access->currentTimingEpoch.store(rtTimingEpoch,std::memory_order_release);
    const auto token=accepting.load(std::memory_order_acquire); if(!token) return false;
    writers.fetch_add(1,std::memory_order_acq_rel);
    if(token!=accepting.load(std::memory_order_acquire)) { writers.fetch_sub(1,std::memory_order_release); return true; }
    heartbeat.fetch_add(1,std::memory_order_relaxed);
    const int frames=input.getNumSamples(),ch=input.getNumChannels();
    const auto write=writeIndex.load(std::memory_order_relaxed);
    const auto parts=frames>0 ? size_t((frames+255)/256) : size_t(0);
    const auto free=(readIndex.load(std::memory_order_acquire)+slots-write-1)%slots;
    constexpr auto limit=std::numeric_limits<std::int64_t>::max()/4;
    int reason=0;
    if(!allowed) reason=3;
    else if(!playing) { if(started.load(std::memory_order_acquire)) reason=1; }
    else if(!valid || frames<1 || frames>8192 || ch<1 || ch>2 || position < -limit || position>limit) reason=3;
    else if(parts>free) reason=2;
    else
    {
        const auto config=configuration.load(std::memory_order_acquire);
        for(size_t part=0;part<parts;++part)
        {
            const int offset=int(part)*256,count=std::min(256,frames-offset);
            auto& b=(*queue)[(write+part)%slots]; b.position=position+offset; b.frames=count; b.channels=ch; b.clock=clock; b.config=config; b.epoch=token; b.timing=rtTimingEpoch; b.signature=signature;
            for(int c=0;c<ch;++c) for(int i=0;i<count;++i) b.pcm[size_t(i*ch+c)]=input.getSample(c,offset+i);
        }
        started.store(true,std::memory_order_release);
        writeIndex.store((write+parts)%slots,std::memory_order_release);
    }
    if(reason && !access->active.load(std::memory_order_acquire)) { writers.fetch_sub(1,std::memory_order_release); return true; }
    if(reason) { int empty=0; terminal.compare_exchange_strong(empty,reason); accepting.store(0,std::memory_order_release); }
    writers.fetch_sub(1,std::memory_order_release); return true;
}
void ACaptureSession::publish()
{
    if(draft && draft->frames) { ++draft->revision; auto progress=std::make_shared<ACaptureData>(*draft); progress->frames=binOffset; state.shown=std::move(progress); }
    else state.shown=state.held;
    access->publish(state,stateGeneration);
}
void ACaptureSession::begin()
{
    if(ownsGate || !access->store.isCurrent(stateGeneration)) return;
    pauseObservation();
    { const juce::ScopedLock lock(control);
        if(rate<8000 || channels<1 || channels>2 || receiver.length()>160) { state.message="Audio input unavailable"; publish(); return; }
        draft=std::make_shared<ACaptureData>(); draft->receiver=receiver; draft->rate=rate; draft->channels=channels; activeConfig=configuration; draft->inputConfiguration=activeConfig; draft->runtimeToken=runtimeToken;
    }
    meter=kirin_reference_capture_create(uint32_t(draft->rate),uint32_t(draft->channels));
    if(!meter || !gate || !gate(true))
    { kirin_reference_visual_drop(meter); meter=nullptr; draft.reset(); state.message="Capture unavailable / finish other capture or Blind"; { const juce::ScopedLock lock(control); paused=false; } publish(); return; }
    kirin_reference_index_drop(captureIndex);
    captureIndex=kirin_reference_index_create(uint32_t(draft->rate),uint32_t(draft->channels),uint32_t(draft->rate)); unitFrames=0;
    ownsGate=true; draft->id=juce::Uuid().toDashedString(); draft->created=juce::Time::currentTimeMillis();
    draft->hop=std::uint64_t(draft->rate/10); draft->bins.reserve(2048); draft->units.reserve(7200); pendingFrames=binOffset=pendingHash=0; measurementFailed=false; started=false;
    access->framesProcessed=0; state.message={}; state.phase=ACapturePhase::armed; terminal=0; access->active=true; access->analysisAvailable=true; access->capturedView=true;
    accepting.store(++epoch,std::memory_order_release); publish();
}
void ACaptureSession::finishBin()
{
    if(!pendingFrames) return;
    ACaptureBin bin; bin.offset=binOffset; bin.fingerprint=pendingHash;
    if(!kirin_reference_capture_finish(meter,&bin.value,&bin.truePeak) || bin.value.frames!=pendingFrames) { terminal=3; accepting=0; measurementFailed=true; return; }
    if(draft->bins.size()==2048) { mergeACaptureBins(draft->bins,draft->channels); draft->hop*=2; }
    draft->bins.push_back(bin); binOffset+=pendingFrames; pendingFrames=pendingHash=0;
}
void ACaptureSession::consume(const Block& b)
{
    if(!draft || !meter || measurementFailed || terminal.load()==5 || b.epoch!=epoch) return;
    if(b.config!=activeConfig || b.channels!=draft->channels || (draft->frames && (b.position!=draft->hostStart+std::int64_t(draft->frames) || b.clock!=draft->clockSource || b.timing!=draft->timingEpoch)))
    { terminal=2; accepting=0; measurementFailed=true; return; }
    if(!draft->frames) { draft->hostStart=b.position; draft->clockSource=b.clock; draft->timingEpoch=b.timing; draft->clockSignature=b.signature; state.phase=ACapturePhase::capturing; }
    const auto remaining=std::uint64_t(draft->rate)*7200-draft->frames;
    const auto count=int(std::min(remaining,std::uint64_t(b.frames)));
    for(int offset=0;offset<count;)
    {
        // Keep the final full bin open until another input frame arrives. At stop, its TP
        // must include the interpolator tail just like a shorter final bin does.
        if(pendingFrames==draft->hop) finishBin();
        if(measurementFailed) return;
        const auto n=int(std::min({std::uint64_t(count-offset),draft->hop-pendingFrames,std::uint64_t(draft->rate)-unitFrames}));
        if(!kirin_reference_visual_push(meter,b.pcm.data()+size_t(offset*b.channels),size_t(n*b.channels))) { terminal=3; accepting=0; measurementFailed=true; return; }
        if(!kirin_reference_index_push(captureIndex,b.pcm.data()+size_t(offset*b.channels),size_t(n*b.channels))) { terminal=3; accepting=0; measurementFailed=true; return; }
        unitFrames+=std::uint64_t(n);
        if(unitFrames==std::uint64_t(draft->rate)) finishUnit();
        pendingHash=captureHash(pendingHash,b.pcm.data()+size_t(offset*b.channels),size_t(n*b.channels));
        pendingFrames+=std::uint64_t(n); draft->frames+=std::uint64_t(n); offset+=n;
    }
    access->framesProcessed.store(draft->frames,std::memory_order_release);
    if(std::uint64_t(count)==remaining) { terminal=4; accepting=0; }
}
void ACaptureSession::close(int reason)
{
    if(draft && reason!=5) { kirin_reference_capture_totals(meter,&draft->integrated,&draft->maximumTruePeak); finishBin(); }
    if(reason!=5 && measurementFailed) reason=terminal.load();
    if(draft && draft->frames && reason!=5)
    {
        finishUnit(); draft->frames=binOffset; draft->terminationReason=reason;
        draft->complete=reason==1;
        if(!draft->complete) draft->integrated=std::numeric_limits<double>::quiet_NaN();
        stampReceipt(*draft); ++draft->revision;
        if(draft->complete || !state.held)
        {
            auto held=std::make_shared<const ACaptureData>(*draft); auto encoded=encodeACapture(*draft);
            if(access->store.commit(stateGeneration,held,encoded))
            { state.held=std::move(held); state.encoded=std::move(encoded); state.revisited.assign(draft->bins.size(),0); state.unitStatus.assign(draft->units.size(),0); state.timingVerified=true; state.confirmedTimingEpoch=draft->timingEpoch; state.observationFresh=false; state.revisitedWork={}; }
        }
        state.phase=draft->complete ? ACapturePhase::held : ACapturePhase::partial;
        state.message=reason==2 ? (state.held && state.held->complete ? "Interrupted / previous capture kept" : "Interrupted / capture again") : reason==3 ? "Input unavailable / capture again" : reason==4 ? "Capture limit reached" : juce::String();
    }
    else { state.phase=reason==2 || reason==3 ? ACapturePhase::partial : state.held ? ACapturePhase::held : ACapturePhase::idle; state.message=reason==2 || reason==3 ? "Input interrupted / capture again" : juce::String(); }
    publish(); // The host dirty notification on gate release must see the committed snapshot.
    draft.reset(); kirin_reference_visual_drop(meter); meter=nullptr;
    kirin_reference_index_drop(captureIndex); captureIndex=nullptr; unitFrames=0;
    if(ownsGate && gate) gate(false); ownsGate=false; access->active=false; access->analysisAvailable=false; terminal=0;
    { const juce::ScopedLock lock(control); paused=false; } publish();
}
void ACaptureSession::run()
{
    double nextPublish=0,lastInput=juce::Time::getMillisecondCounterHiRes(); std::uint64_t lastHeartbeat=0;
    while(!threadShouldExit())
    {
        const auto now=juce::Time::getMillisecondCounterHiRes();
        std::uint64_t commandGeneration=0;
        const int command=access->takeCommand(commandGeneration);
        if(command==ACaptureAccess::start && !ownsGate) { stateGeneration=commandGeneration; begin(); lastInput=now; lastHeartbeat=heartbeat; }
        if(ownsGate && (command==ACaptureAccess::finish || command==ACaptureAccess::cancel)) { accepting=0; if(command==ACaptureAccess::cancel) terminal=5; else { int empty=0; terminal.compare_exchange_strong(empty,1); } }
        serviceRestore();
        if(ownsGate && activeConfig!=configuration.load()) { accepting=0; terminal=2; }
        const auto beat=heartbeat.load(); if(beat!=lastHeartbeat) { lastInput=now; lastHeartbeat=beat; }
        if(ownsGate && draft && draft->frames && now-lastInput>750 && !terminal.load()) { accepting=0; terminal=3; }
        if(!ownsGate) { prepareObservation(); if(now>=nextEvidencePoll) { stampHeldReceipt(); nextEvidencePoll=now+100; } }
        const auto read=readIndex.load(std::memory_order_relaxed);
        if(read!=writeIndex.load(std::memory_order_acquire)) { if(ownsGate) consume((*queue)[read]); else consumeRevisit((*queue)[read]); readIndex.store((read+1)%slots,std::memory_order_release); }
        if(ownsGate && terminal.load() && writers.load(std::memory_order_acquire)==0 && readIndex.load()==writeIndex.load()) close(terminal.load());
        else if(ownsGate && now>=nextPublish) { publish(); nextPublish=now+100; }
        if(readIndex.load()==writeIndex.load()) access->wake.wait(ownsGate || access->analysisAvailable ? 1 : 250);
    }
}
}
