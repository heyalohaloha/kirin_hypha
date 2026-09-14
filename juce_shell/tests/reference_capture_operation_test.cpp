#include "../src/reference_audition/ReferenceComparisonController.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
namespace ref=hypha::reference_audition;
namespace { void require(bool ok,const char* why) { if(!ok) { std::cerr<<why<<std::endl; std::exit(1); } } }
namespace {
void testCaptureOperationBoundary()
{
    auto owner=std::make_shared<ref::ReferenceAnalysis>(); auto old=owner->acquire();
    require(bool(old),"replacement fixture owns an analysis slot");
    owner->replace(kirin_reference_analysis_create());
    require(!owner->current(old) && !owner->acquire(),"new engine waits for old jobs instead of doubling its physical demand");
    old.reset(); require(bool(owner->acquire()),"replacement can admit after its old job retires");
    ref::ACaptureAccess a; std::uint64_t delivered=0;
    require(a.request(ref::ACaptureAccess::start,0),"first click binds idle generation zero");
    const auto first=a.operationView().id;
    require(a.takeCommand(delivered)==ref::ACaptureAccess::start && delivered==first,"worker receives the same operation");
    require(!a.reserveBlind(ref::CaptureBlindOwner::version) && !a.reserveBlind(ref::CaptureBlindOwner::local),"both Blind starts respect an admission-in-flight Capture reservation");
    require(!a.request(ref::ACaptureAccess::start) && a.busy(),"delivery never reopens Start or Blind eligibility");
    require(a.advance(first,ref::CaptureOperationPhase::finalizing),"fixture enters finalization");
    require(!a.request(ref::ACaptureAccess::start),"finalization reserves capture through release");
    require(a.request(ref::ACaptureAccess::cancel,first),"cancel may linearize before commit");
    require(!a.commitAttempt(first,{},{}),"cancelled finalization never commits");
    a.complete(first);
    require(!a.request(ref::ACaptureAccess::start,0),"old initial click is stale after another completed capture");
    require(a.request(ref::ACaptureAccess::start,first),"next explicit capture has a new operation");
    const auto second=a.operationView().id;
    require(!a.request(ref::ACaptureAccess::cancel,first) && !a.request(ref::ACaptureAccess::finish,first),"old stop and cancel cannot affect a new operation");
    const auto restored=a.beginRestore({},false);
    require(!a.request(ref::ACaptureAccess::start) && !a.advance(second,ref::CaptureOperationPhase::armed),"restore invalidates late admission and reserves Start");
    a.complete(second); require(a.operationView().phase==ref::CaptureOperationPhase::restoring,"old completion cannot end restoration");
    require(a.finishRestore(restored,{}),"empty saved state restores"); a.completeRestore(restored);
    require(a.reserveBlind(ref::CaptureBlindOwner::local) && !a.request(ref::ACaptureAccess::start),"reserved Local Blind closes the inverse Capture race");
    a.releaseBlind(ref::CaptureBlindOwner::version); require(!a.request(ref::ACaptureAccess::start),"different Blind cannot release another owner");
    a.releaseBlind(ref::CaptureBlindOwner::local);
    require(a.request(ref::ACaptureAccess::start,restored),"new explicit capture after restoration");
    a.close(); require(!a.request(ref::ACaptureAccess::start),"closed access cannot reopen");
}
void testAdmissionCancellation(bool restore)
{
    std::atomic<bool> entered{false},release{false}; std::atomic<int> balance{0};
    ref::ACaptureSession session([&](bool active) { if(active) { ++balance; entered=true; while(!release) juce::Thread::sleep(1); } else --balance; return true; });
    session.configure("cancel-start",48000,2);
    require(session.access->request(ref::ACaptureAccess::start),"delayed admission starts");
    for(int i=0;i<800 && !entered;++i) juce::Thread::sleep(5);
    require(entered,"admission barrier reached");
    const auto token=session.access->operationView().id;
    if(restore) session.restore({},false); else require(session.access->request(ref::ACaptureAccess::cancel,token),"cancel starting operation");
    release=true;
    for(int i=0;i<800 && session.access->busy();++i) juce::Thread::sleep(5);
    require(!session.access->busy() && balance==0 && !session.access->active && !session.access->snapshot().shown,"late admission retires exactly once without publishing a draft");
}
}
void testCaptureStartRace();
void testCaptureStartRace() {
    testCaptureOperationBoundary(); testAdmissionCancellation(false); testAdmissionCancellation(true);
    std::atomic<bool> entering{false}, release{false};
    ref::ACaptureSession capture([&](bool on){if(on){entering=true;while(!release)juce::Thread::sleep(1);}return true;});
    capture.configure("review-start-race",48000,2);
    const auto wait=[](const auto& f){for(int i=0;i<800;++i){if(f())return true;juce::Thread::sleep(5);}return false;};
    require(capture.access->request(ref::ACaptureAccess::start),"first start accepted");
    require(wait([&]{return entering.load();}),"worker entered admission");
    const bool duplicate=capture.access->request(ref::ACaptureAccess::start);
    release=true;
    require(wait([&]{return capture.access->active.load();}),"capture starts");
    juce::AudioBuffer<float> input(2,4800);
    for(int i=0;i<10;++i){for(int c=0;c<2;++c)for(int f=0;f<4800;++f)input.setSample(c,f,float(0.25*std::sin((i*4800+f)*0.02)));capture.observe(input,i*4800,true,true,true,1);juce::Thread::sleep(15);}
    capture.observe(input,48000,true,false,true,1);
    require(wait([&]{return !capture.access->active.load();}),"capture finishes");
    const auto saved=capture.access->snapshot();
    require(!duplicate && saved.held && saved.held->frames==48000 && !capture.access->store.value().encoded.isEmpty(),"duplicate start preserves one complete capture");
    std::cout << "duplicate_start_accepted=" << duplicate << " processed_frames=" << capture.access->framesProcessed << " held=" << bool(saved.held) << " saved_bytes=" << capture.access->store.value().encoded.length() << " message=" << saved.message << std::endl;
}
