#include "../src/reference_audition/ReferenceACaptureSession.h"
#include "reference_runtime_test_support.h"
#include "reference_rt_probe.h"
#include <iostream>
void testReferenceACaptureLong();
#include <algorithm>
void testReferenceACaptureLong()
{
    ref::ACaptureSession capture([](bool){return true;});capture.configure("long-capture",8000,1);
    require(capture.access->request(ref::ACaptureAccess::start),"long capture request");
    for(int i=0;i<600 && !capture.access->active;++i)juce::Thread::sleep(5);
    require(capture.access->active,"long capture armed");
    constexpr std::uint64_t total=8000ULL*7200;
    juce::AudioBuffer<float> audio(1,8192);
    std::vector<double> callbacks;
    const auto started=juce::Time::getMillisecondCounterHiRes();
    for(std::uint64_t position=0;position<total;)
    {
        const int frames=int(std::min<std::uint64_t>(8192,total-position));
        for(int i=0;i<frames;++i) audio.setSample(0,i,float(0.1*std::sin(juce::MathConstants<double>::twoPi*1000.0*double(position+std::uint64_t(i))/8000.0)));
        juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(),1,frames);
        while(position>capture.access->framesProcessed+8192*8 && capture.access->active)juce::Thread::sleep(1);
        require(capture.access->active,"long capture has no queue discontinuity");
        beginReferenceRtProbe();const auto before=juce::Time::getMillisecondCounterHiRes();
        capture.observe(block,std::int64_t(position),true,true,true,1);
        const auto elapsed=juce::Time::getMillisecondCounterHiRes()-before;
        require(endReferenceRtProbe()==0,"long capture callback allocation remains zero");
        callbacks.push_back(elapsed);position+=std::uint64_t(frames);
    }
    for(int i=0;i<12000 && capture.access->active;++i)juce::Thread::sleep(5);
    const auto state=capture.access->snapshot();
    require(!capture.access->active && state.held && state.held->frames==total,"two-hour limit keeps every accepted frame");
    require(!state.held->complete && state.phase==ref::ACapturePhase::partial,"limit never claims whole-song completion");
    require(state.held->bins.size()<=2048 && state.encoded.length()<1024*1024,"two-hour capture respects bin and state budgets");
    require(ref::decodeACapture(state.encoded)!=nullptr,"long coarsened snapshot restores");
    std::sort(callbacks.begin(),callbacks.end());
    std::cout<<"Capture A 2h: "<<state.held->frames<<" frames, "<<state.held->bins.size()<<" bins, "<<state.encoded.length()<<" serialized bytes; callback p95 "
        <<callbacks[size_t(callbacks.size()*0.95)]<<" ms / p99 "<<callbacks[size_t(callbacks.size()*0.99)]<<" ms; elapsed "<<(juce::Time::getMillisecondCounterHiRes()-started)/1000<<" s\n";
}
