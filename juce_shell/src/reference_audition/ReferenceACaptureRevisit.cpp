#include "ReferenceACaptureSession.h"
#include <algorithm>
namespace hypha::reference_audition
{
void ACaptureSession::setPresented(bool value)
{
    const juce::ScopedLock lock(control); presented=value;
    if(!value && !access->active) { accepting=0; kirin_reference_visual_admission_set(observationAdmission,false); access->analysisAvailable=false; }
}
void ACaptureSession::pauseObservation()
{
    const juce::ScopedLock lock(control); paused=true;
    if(!access->active) accepting=0;
    kirin_reference_visual_admission_set(observationAdmission,false);
    if(!access->active) access->analysisAvailable=false;
}
void ACaptureSession::useAuditionAdmission(bool value)
{ const juce::ScopedLock lock(control); borrowed=value; paused=false; }
void ACaptureSession::prepareObservation()
{
    const juce::ScopedLock lock(control);
    const bool wanted=presented && !paused && state.held && access->capturedView
        && (state.held->restored || receiver==state.held->receiver)
        && rate==state.held->rate && channels==state.held->channels;
    if(!wanted) { accepting=0; kirin_reference_visual_admission_set(observationAdmission,false); access->analysisAvailable=false; return; }
    const bool admitted=borrowed || kirin_reference_visual_admission_set(observationAdmission,true);
    access->analysisAvailable=admitted;
    if(admitted && !accepting && !writers.load() && readIndex.load()==writeIndex.load())
    { revisitExpected=0; revisitFrames=matchingFrames=0; accepting.store(++epoch,std::memory_order_release); }
}
void ACaptureSession::stampReceipt(ACaptureData& data)
{
    if(!receipt || data.frames<std::uint64_t(data.rate)*4) return;
    const auto map=receipt();
    if(map.verified && map.rate==data.rate && map.hostPosition>=data.hostStart
        && map.hostPosition<=data.hostStart+std::int64_t(data.frames)) data.verifiedWork=map.work;
}
void ACaptureSession::consumeRevisit(const Block& block)
{
    if(!state.held || !access->analysisAvailable || block.epoch!=epoch) return;
    const auto& data=*state.held;
    if(block.channels!=data.channels || block.clock!=data.clockSource || block.config!=configuration.load())
    { revisitFrames=matchingFrames=0; return; }
    auto at=block.position-data.hostStart;
    if(at<0 || at>=std::int64_t(data.frames)) { revisitFrames=matchingFrames=0; return; }
    if(block.position!=revisitExpected) { revisitFrames=matchingFrames=0; }
    int offset=0;
    while(offset<block.frames && at<std::int64_t(data.frames))
    {
        if(!revisitFrames)
        {
            const auto found=std::upper_bound(data.bins.begin(),data.bins.end(),std::uint64_t(at),
                [](auto v,const auto& b){return v<b.offset;});
            if(found==data.bins.begin()) break;
            revisitIndex=size_t((found-data.bins.begin())-1); revisitHash=0;
        }
        const auto& bin=data.bins[revisitIndex];
        const auto last=bin.offset+bin.value.frames;
        const int n=int(std::min(std::uint64_t(block.frames-offset),last-std::uint64_t(at)));
        if(std::uint64_t(at)==bin.offset+revisitFrames)
        {
            revisitHash=captureHash(revisitHash,block.pcm.data()+size_t(offset*block.channels),size_t(n*block.channels));
            revisitFrames+=std::uint64_t(n);
            if(revisitFrames==bin.value.frames)
            {
                const bool same=revisitHash==bin.fingerprint;
                state.revisited[revisitIndex]=same ? 1 : 2;
                matchingFrames=same && std::max(bin.value.rms[0],bin.value.rms[1])>0.001 ? matchingFrames+revisitFrames : 0;
                if(matchingFrames>=std::uint64_t(data.rate)*4 && receipt)
                {
                    const auto map=receipt(); const auto end=data.hostStart+std::int64_t(last);
                    if(map.verified && map.rate==data.rate && map.hostPosition>=end-std::int64_t(matchingFrames)
                        && map.hostPosition<=end) state.revisitedWork=map.work;
                }
                revisitFrames=0; publish();
            }
        }
        else matchingFrames=0;
        offset+=n; at+=n;
    }
    revisitExpected=block.position+block.frames;
}
}
