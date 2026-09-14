#include "kirin_hypha_reference_capture_ffi.h"
#include "ReferenceACaptureProjection.h"
#include "ReferenceVisualAudio.h"
namespace hypha::reference_audition
{
ACaptureProjection::ACaptureProjection(std::shared_ptr<ACaptureAccess> value,std::function<VisualBinding()> provider,std::shared_ptr<ReferenceAnalysis> owner)
    :juce::Thread("Captured A comparison"),analysis(std::move(owner)),access(std::move(value)),binding(std::move(provider))
{ startThread(juce::Thread::Priority::low); }
ACaptureProjection::~ACaptureProjection() { signalThreadShouldExit(); notify(); stopThread(-1); }
std::shared_ptr<const VisualTimeline> ACaptureProjection::snapshot() const
{ const juce::ScopedLock lock(mutex); return published; }
void ACaptureProjection::run()
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::AudioBuffer<float> audio,scratch; std::array<float,16384> pcm{};
    VisualTimeline result; juce::String key; std::uint64_t hop=0,revision=0; size_t index=0;
    KirinReferenceVisualMeter* meter=nullptr; std::uint64_t consumed=0;
    double nextPublish=0; bool dirty=true;
    while(!threadShouldExit())
    {
        if(!presented || !access->capturedView) { wait(100); continue; }
        const auto state=access->snapshot(); const auto data=state.shown;
        if(!data || data->bins.empty()) { { const juce::ScopedLock lock(mutex); published.reset(); } wait(50); continue; }
        auto job=access->analysisAvailable ? analysis->acquire() : ReferenceAnalysis::Lease{};
        auto map=binding();
        // A historical plot has its own immutable position and gain receipt. Live map/gain
        // revisions can never move or level the saved pass, including after state restore.
        const CaptureBindingReceipt* proof=nullptr;
        if(map.source && !map.hidden) for(const auto& item:data->bindings)
            if(item.valid() && item.captureId==data->id && item.sourceHash==map.source->sourceFileSha256
                && item.sourcePcmHash==map.source->sourcePcmSha256 && item.rate==data->rate && item.channels==data->channels) { proof=&item; break; }
        RuntimeV2SourceRepository verifier(juce::File{});
        map.aligned=proof && verifier.verifySourceRevision(*map.source).isEmpty();
        if(map.aligned)
        {
            map.hostAnchor=proof->hostAnchor; map.sourceAnchor=proof->sourceAnchor;
            map.gainDb=proof->gainKnown ? proof->displayGainDb : 0;
            map.matched=proof->gainKnown && !proof->originalFallback;
            map.key=data->id+":"+proof->sourceHash+":"+juce::String(proof->revision);
        }
        else { map.source.reset(); map.overview.reset(); map.gainDb=0; map.matched=false; map.key=data->id+":a-only"; }
        map.captureEvidence.reset();
        map.hostRate=data->rate; map.channels=data->channels;
        if(key!=map.key || hop!=data->hop)
        {
            dirty=true; key=map.key; hop=data->hop; index=0; consumed=0;
            result={}; result.binding=map;
            kirin_reference_visual_drop(meter); meter=nullptr; reader.reset();
        }
        if(map.aligned && job && (!reader || !meter)) {
            reader.reset(formats.createReaderFor(juce::File(map.source->absolutePath)));
            kirin_reference_visual_drop(meter); meter=kirin_reference_visual_create(uint32_t(data->rate),uint32_t(data->channels));
        }
        const bool captureChanged=result.capture!=data;
        dirty=dirty || captureChanged || result.revisited!=state.revisited;
        result.capture=data; result.revisited=state.revisited; result.binding=map; result.pass=1;
        result.bins.resize(data->bins.size());
        if(captureChanged) for(size_t i=0;i<data->bins.size();++i) { result.bins[i].a=data->bins[i].value; result.bins[i].pass=1; }
        if(map.aligned && reader && meter && index<data->bins.size() && job)
        {
            const auto& bin=data->bins[index]; std::int64_t mapped=0;
            const auto position=data->hostStart+std::int64_t(bin.offset+consumed);
            const int frames=int(std::min<std::uint64_t>(8192,bin.value.frames-consumed));
            const auto sourceEnd=VisualTimeline::outputSample(map.source->audio.totalSampleFrames,map.source->audio.sampleRateHz,data->rate);
            const bool interval=map.mapPosition(position,mapped) && mapped>=0 && mapped+frames<=sourceEnd;
            bool valid=interval && readReferenceVisualAudio(*reader,mapped,frames,data->rate,data->channels,audio,scratch,
                [this,&job](const auto& convert) { if(!analysis->current(job) || !access->analysisAvailable || !presented || threadShouldExit()) return false; convert(); return true; });
            if(valid)
            {
                for(int c=0;c<data->channels;++c) for(int i=0;i<frames;++i) pcm[size_t(i*data->channels+c)]=audio.getSample(c,i);
                valid=kirin_reference_visual_push(meter,pcm.data(),size_t(frames*data->channels));
            }
            if(valid && analysis->current(job) && presented && access->capturedView)
            {
                consumed+=std::uint64_t(frames);
                if(consumed==bin.value.frames)
                {
                    if(index+1==data->bins.size() && !access->active) { double loudness=0,tp=0; kirin_reference_capture_totals(meter,&loudness,&tp); }
                    KirinReferenceVisualBin b{};
                    if(kirin_reference_visual_finish(meter,&b) && b.frames==bin.value.frames) result.bins[index].b=b;
                    dirty=true;
                    ++index; consumed=0;
                }
            }
            else if(!interval)
            { ++index; consumed=0; kirin_reference_visual_drop(meter); meter=kirin_reference_visual_create(uint32_t(data->rate),uint32_t(data->channels)); }
            else { wait(25); continue; }
        }
        const auto now=juce::Time::getMillisecondCounterHiRes();
        if(dirty && now>=nextPublish && presented && access->capturedView && access->snapshot().shown==data
            && (!job || analysis->current(job)))
        {
            // Recheck immutable B after reads. Failed B provenance never removes captured A.
            if(map.source && verifier.verifySourceRevision(*map.source).isNotEmpty()) { key={}; continue; }
            result.revision=++revision; auto next=std::make_shared<const VisualTimeline>(result);
            { const juce::ScopedLock lock(mutex); published=std::move(next); }
            nextPublish=now+100; dirty=false;
        }
        job.reset();
        wait(index<data->bins.size() && map.aligned && access->analysisAvailable ? 1 : 100);
    }
    kirin_reference_visual_drop(meter);
}
}
