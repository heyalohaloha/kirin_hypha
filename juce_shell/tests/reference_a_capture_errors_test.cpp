#include "../src/reference_audition/ReferenceACaptureSession.h"
#include "reference_runtime_test_support.h"
void testReferenceACaptureErrors();
void testReferenceACaptureErrors()
{
    const auto wait=[](const auto& test) {for(int i=0;i<600;++i){if(test())return true;juce::Thread::sleep(5);}return false;};
    for(int failure=0;failure<8;++failure)
    {
        ref::ACaptureSession capture([](bool){return true;});capture.configure("errors",48000,2);
        require(capture.access->request(ref::ACaptureAccess::start),"failure fixture starts");
        require(wait([&]{return capture.access->active.load();}),"failure fixture armed");
        juce::AudioBuffer<float> audio(2,8192);for(int c=0;c<2;++c) for(int i=0;i<8192;++i) audio.setSample(c,i,0.1f);
        capture.observe(audio,0,true,true,true,1);
        require(wait([&]{return capture.access->framesProcessed==8192;}),"accepted prefix before interruption");
        switch(failure)
        {
            case 0: capture.observe(audio,8192,false,true,true,1);break;
            case 1: capture.observe(audio,8192,true,true,false,1);break;
            case 2: capture.observe(audio,8192,true,true,true,2);break;
            case 3: capture.configure("errors",96000,2);break;
            case 4: audio.setSample(0,0,NAN);capture.observe(audio,8192,true,true,true,1);break;
            case 5: for(int i=1;i<600;++i) capture.observe(audio,std::int64_t(i)*8192,true,true,true,1);break;
            case 7: capture.observe(audio,8192,true,true,true,1,{64,0,1,true,false});break;
            case 6: { juce::AudioBuffer<float> mono(audio.getArrayOfWritePointers(),1,8192);capture.observe(mono,8192,true,true,true,1);break; }
        }
        require(wait([&]{return !capture.access->active;}),"clock/bypass/rate/nonfinite/overflow/channel failure closes without blocking audio");
        const auto state=capture.access->snapshot();
        require(state.phase==ref::ACapturePhase::partial && state.held && !state.held->complete,"failure is partial, never whole-song success");
        require(state.held->frames>=8192 && std::isnan(state.held->integrated) && state.message.isNotEmpty(),"partial keeps accepted prefix and explicit short notice");
        require(ref::decodeACapture(state.encoded)!=nullptr,"partial summary remains bounded and restorable");
        if(failure==0) {
            capture.restore(state.encoded);
            require(wait([&]{const auto v=capture.access->snapshot();return v.held && v.held->restored;}),"partial restore completes");
            const auto restored=capture.access->snapshot();
            require(restored.phase==ref::ACapturePhase::partial && restored.message.isNotEmpty(),"partial and its notice survive restore");
        }
    }
    {
        ref::ACaptureSession capture([](bool){return true;});capture.configure("tail",48000,1);
        capture.access->request(ref::ACaptureAccess::start);
        require(wait([&]{return capture.access->active.load();}),"exact-bin tail fixture armed");
        juce::AudioBuffer<float> audio(1,4800);audio.clear();
        for(int i=4797;i<4800;++i) audio.setSample(0,i,0.8f);
        capture.observe(audio,0,true,true,true,1);
        require(wait([&]{return capture.access->framesProcessed==4800;}),"exact full bin accepted before stop");
        capture.observe(audio,4800,true,false,true,1);
        require(wait([&]{return !capture.access->active;}),"exact-bin tail closes");
        const auto held=capture.access->snapshot().held;
        require(held && held->bins.size()==1 && held->maximumTruePeak>0.8,"tail fixture includes intersample overshoot");
        require(std::abs(held->bins[0].truePeak-held->maximumTruePeak)<1e-9,"last complete bin retains the final TP filter tail");
    }
    std::cout<<"Capture A errors: missing clock, offline/bypass, clock change, rate change, nonfinite input, saturated queue, channel change PASS\n";
}
