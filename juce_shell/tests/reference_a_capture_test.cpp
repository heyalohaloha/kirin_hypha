#include "../src/reference_audition/ReferenceComparisonController.h"
#include "../src/reference_audition/ReferenceACaptureSession.h"
#include "../src/reference_audition/ReferenceACaptureProjection.h"
#include "reference_whole_song_fixture.h"
#include "reference_rt_probe.h"
#include "ReferenceCaptureStoreTest.h"
#include "ReferenceCaptureStorageBudgetTest.h"
#include <iostream>
namespace
{
bool waitCapture(const std::function<bool()>& f,int ms=3000)
{ for(int i=0;i<ms/5;++i) { if(f()) return true; juce::Thread::sleep(5); } return false; }
}
void testReferenceACaptureProjection(const juce::File&,const std::shared_ptr<const ref::ACaptureData>&,const juce::var&);
void testReferenceACaptureLong();
void testReferenceACaptureErrors();
void testReferenceACapture(const juce::File&);
void testReferenceACapture(const juce::File& root)
{
    auto fixture=makeWholeSongFixture(root,root.getChildFile("capture-source.wav"),"recording-capture","version-capture");
    std::atomic<int> owns{0}; std::atomic<bool> admit{true};
    ref::ACaptureSession capture([&](bool active){ if(active && !admit) return false; owns=active ? 1 : 0; return true; });
    capture.configure("capture-instance",48000,2);
    require(capture.access->request(ref::ACaptureAccess::start),"capture accepts explicit start");
    require(waitCapture([&]{return capture.access->active.load();}),"capture is armed without B or OS");
    juce::AudioBuffer<float> audio(2,4800); audio.clear();
    capture.observe(audio,0,true,false,true,1); juce::Thread::sleep(30);
    require(capture.access->active && capture.access->snapshot().phase==ref::ACapturePhase::armed,"stopped callbacks do not cancel arming");
    const auto feed=[&](int first,int count,bool altered=false) {
        for(int i=first;i<first+count;++i) {
            for(int c=0;c<2;++c) audio.copyFrom(c,0,fixture.audio,c,i*4800,4800);
            if(altered) audio.applyGain(0.5f);
            beginReferenceRtProbe(); const bool copied=capture.observe(audio,24000+i*4800,true,true,true,1);
            const auto allocations=endReferenceRtProbe();
            require(copied && allocations==0,"Capture callback copies without allocation");
            const float observed=audio.getSample(0,123),expected=fixture.audio.getSample(0,i*4800+123)*(altered ? 0.5f : 1.0f);
            require(std::memcmp(&observed,&expected,sizeof(float))==0,"original A is bit identical");
            juce::Thread::sleep(10);
        }
    };
    feed(0,40);
    capture.observe(audio,24000+40*4800,true,false,true,1);
    require(waitCapture([&]{return !capture.access->active;}),"DAW stop finalizes capture");
    const auto held=capture.access->snapshot();
    require(held.held && held.held->complete && held.held->frames==192000 && held.held->hostStart==24000,"exact accepted four-second range retained");
    require(owns==0,"capture admission released on finish");
    require(held.held->bins.size()==40 && std::isfinite(held.held->integrated),"waveform and global integrated loudness retained");
    for(size_t i=0;i<40;++i) {
        const auto& bin=held.held->bins[i]; double peak=0,energy=0;
        for(int f=0;f<4800;++f) { const auto v=fixture.audio.getSample(0,int(i)*4800+f); peak=std::max(peak,std::abs(double(v))); energy+=double(v)*v; }
        require(std::abs(bin.value.peak[0]-peak)<1e-9 && std::abs(bin.value.rms[0]-std::sqrt(energy/4800))<1e-9,"raw independent peak/RMS reference calculation");
    }
    testCaptureStore(held.held,held.encoded);
    testCaptureStorageBudget(*held.held);
    const auto restored=ref::decodeACapture(held.encoded);
    require(restored && restored->restored && restored->frames==held.held->frames && restored->bins[0].fingerprint==held.held->bins[0].fingerprint,"bounded snapshot restores history and fingerprints");
    require(held.encoded.length()<256*1024 && !ref::decodeACapture(held.encoded+"x") && !ref::decodeACapture("old state"),"corrupt and old state rejected");
    auto merged=held.held->bins; ref::mergeACaptureBins(merged,2);
    std::vector<float> pcm; for(int f=0;f<9600;++f) for(int c=0;c<2;++c) pcm.push_back(fixture.audio.getSample(c,f));
    require(merged[0].fingerprint==ref::captureHash(0,pcm.data(),pcm.size()) && merged[0].value.frames==9600,"coarsening retains exact concatenated fingerprint and frames");
    capture.setPresented(true);
    require(waitCapture([&]{return capture.access->analysisAvailable.load();}),"held view admits lightweight revisits");
    feed(0,12,true);
    require(waitCapture([&]{const auto s=capture.access->snapshot();return s.revisited.size()==40 && s.revisited[1]==2;}),"revisited changed input marked without overwriting captured A");
    require(capture.access->snapshot().held->bins[1].fingerprint==held.held->bins[1].fingerprint,"captured original remains immutable");
    capture.setPresented(false);
    require(capture.access->request(ref::ACaptureAccess::start),"capture again");
    require(waitCapture([&]{return capture.access->active.load();}),"new pass starts");
    feed(0,2); feed(10,1);
    require(waitCapture([&]{return !capture.access->active;}),"seek closes partial capture");
    const auto failed=capture.access->snapshot();
    require(failed.phase==ref::ACapturePhase::partial && failed.held->id==held.held->id && failed.encoded==held.encoded,"failed retry preserves previous successful result");
    require(capture.access->request(ref::ACaptureAccess::start),"restart after seek");
    require(waitCapture([&]{return capture.access->active.load();}),"restart armed");
    feed(0,1); require(capture.access->request(ref::ACaptureAccess::cancel),"cancel accepted");
    require(waitCapture([&]{return !capture.access->active;}),"cancel releases admission");
    require(capture.access->snapshot().held->id==held.held->id,"cancel keeps previous capture");
    admit=false; require(capture.access->request(ref::ACaptureAccess::start),"request denied admission");
    require(waitCapture([&]{return capture.access->snapshot().message.contains("unavailable");}),"denied explicit start is visible");
    require(!capture.access->active && owns==0,"denied start cannot consume a hidden slot");
    capture.restore(held.encoded);
    require(capture.access->store.value().encoded==held.encoded,"immediate host save cannot lose pending capture restoration");
    require(waitCapture([&]{const auto s=capture.access->snapshot();return s.held && s.held->restored;}),"state restoration is worker-owned");
    require(!capture.access->active && owns==0,"restore never starts capture or audio");
    {
        ref::ACaptureSession reopened([](bool){return true;});
        reopened.configure("new-instance-after-host-reopen",48000,2); reopened.restore(held.encoded); reopened.setPresented(true);
        require(waitCapture([&]{return reopened.access->analysisAvailable.load();}),"restored capture can revisit in a newly created instance");
        require(reopened.access->snapshot().revisitedWork.isEmpty(),"restored receiver cannot inherit Work qualification");
        for(int pass=0;pass<2;++pass)
        {
            for(int i=0;i<40;++i)
            {
                for(int c=0;c<2;++c) audio.copyFrom(c,0,fixture.audio,c,i*4800,4800);
                if(pass==0) audio.applyGain(0.5f);
                reopened.observe(audio,24000+i*4800,true,true,true,1); juce::Thread::sleep(10);
            }
            require(waitCapture([&]{const auto s=reopened.access->snapshot();return pass==0 ? (!s.unitStatus.empty() && s.unitStatus.back()==3 && !s.timingVerified) : (!s.unitStatus.empty() && s.unitStatus.back()==1 && s.timingVerified);}),"restored revisit compares the full accepted range");
            require(reopened.access->snapshot().revisitedWork.isEmpty(),"input matching never invents Work or B identity");
        }
    }
    testReferenceACaptureProjection(root,held.held,fixture.source["file"]["revision"]);
    if(juce::SystemStats::getEnvironmentVariable("KIRIN_CAPTURE_A_LONG",{})=="1") testReferenceACaptureLong();
    testReferenceACaptureErrors();
    capture.shutdown(); require(!capture.access->request(ref::ACaptureAccess::start),"stale editor mailbox cannot restart a destroyed processor");
    std::cout << "Capture A: exact frames, immutable A, no B dependency, persistence, seek, cancel, denial, revisit, RT allocation PASS\n";
}
