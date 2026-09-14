#include "../src/reference_audition/ReferenceACaptureModel.h"
#include "../src/reference_audition/ReferenceComparisonSettings.h"
#include "kirin_hypha_reference_ffi.h"
#include "kirin_hypha_reference_capture_ffi.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>
#if defined(__APPLE__)
 #include <malloc/malloc.h>
#endif
namespace ref = hypha::reference_audition;
void testReferenceCaptureMemory();
void testReferenceCaptureMemoryCase(int,int);
namespace {
void check(bool ok,const char* why) { if(!ok) { std::cerr<<"Capture RAM: "<<why<<'\n'; std::exit(1); } }
struct Heap { size_t live=0,high=0; };
Heap heap() {
 #if defined(__APPLE__)
    malloc_statistics_t stats{}; malloc_zone_statistics(nullptr,&stats);
    return {stats.size_in_use,stats.max_size_in_use};
 #else
    return {}; // numerical capacities still tested; allocator peak needs the platform harness
 #endif
}
std::shared_ptr<ref::ACaptureData> document(int rate,int channels)
{
    auto value=std::make_shared<ref::ACaptureData>(); value->id=juce::Uuid().toDashedString();
    value->receiver=juce::String::repeatedString("r",160); value->rate=rate; value->channels=channels;
    value->frames=std::uint64_t(rate)*7200; value->hop=value->frames/2048;
    value->units.resize(7200); value->bins.resize(2048); std::uint64_t offset=0;
    for(size_t i=0;i<value->bins.size();++i) {
        auto& bin=value->bins[i]; bin.offset=offset;
        bin.value.frames=value->hop+(i+1==value->bins.size()?value->frames%2048:0); offset+=bin.value.frames;
    }
    for(int i=0;i<16;++i) {
        ref::CaptureBindingReceipt receipt; receipt.captureId=value->id;
        receipt.sourceHash=juce::String(i).paddedLeft('0',64); receipt.sourcePcmHash=receipt.sourceHash;
        receipt.calibrationHash=juce::String::repeatedString("c",64); receipt.work=juce::String::repeatedString("w",160);
        receipt.rate=rate; receipt.channels=channels; receipt.probeEnd=std::int64_t(rate)*4;
        check(receipt.valid(),"max receipt valid"); value->bindings.push_back(receipt);
    }
    return value;
}
struct Lane {
    // Reserve the complete declared queue ceiling, including the existing visual queue.
    std::vector<char> queues=std::vector<char>(2*1024*1024);
    ref::ACaptureStore store;
    std::shared_ptr<ref::ACaptureData> held,draft,published;
    std::shared_ptr<const ref::ACaptureData> restored;
    ref::ACaptureState state,ui;
    juce::String heldText,pendingText,encoded;
    std::unique_ptr<juce::XmlElement> xml;
    void exercise(int rate,int channels) {
        held=document(rate,channels); heldText=ref::encodeACapture(*held);
        check(store.commit(store.beginAttempt(),held,heldText),"held ownership");
        draft=std::make_shared<ref::ACaptureData>(*held); draft->id=juce::Uuid().toDashedString();
        for(auto& proof:draft->bindings) proof.captureId=draft->id;
        published=std::make_shared<ref::ACaptureData>(*draft);
        state.held=held; state.shown=published; state.revisited.resize(2048); state.unitStatus.resize(7200);
        state.unitPass.resize(7200); state.unitCheckedAt.resize(7200); ui=state;
        pendingText=ref::encodeACapture(*draft); const auto token=store.beginRestore(pendingText);
        restored=ref::decodeACapture(pendingText); check(restored!=nullptr,"pending restore decode");
        check(store.value().encoded==pendingText,"immediate resave owns pending payload");
        encoded=ref::encodeACapture(*published); check(encoded.isNotEmpty(),"encode while holding all generations");
        ref::ReferenceComparisonSettings settings; settings.captureState=encoded;
        xml=std::make_unique<juce::XmlElement>("Settings"); settings.write(*xml);
        check(xml->toString().getNumBytesAsUTF8()<1024*1024,"whole state below 1 MiB");
        check(store.finishRestore(token,restored),"generation-specific restore commit");
    }
};
}
void testReferenceCaptureMemoryCase(int rate,int channels)
{
    // Existing short calibration A/B PCM is a baseline, never copied/retained by a Capture receipt.
    // Preallocate once at the maximum rate so lower-rate cases cannot hide behind an older heap peak.
    std::vector<float> a(768000*4*2),b(a.size());
    for(size_t i=0;i<a.size();++i) { a[i]=0.1f*std::sin(float(i)*0.05f); b[i]=a[i]*0.5f; }
    const auto before=heap(); size_t largest=0;
    std::atomic<bool> sampling{true}; std::atomic<size_t> sampledLive{before.live};
    std::thread sampler([&]{
        while(sampling.load()) {
            const auto live=heap().live; auto old=sampledLive.load();
            while(old<live && !sampledLive.compare_exchange_weak(old,live)) {}
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });
    {
        std::array<Lane,2> posts;
        std::array<std::thread,2> workers {
            std::thread([&]{ posts[0].exercise(rate,channels); }),
            std::thread([&]{ posts[1].exercise(rate,channels); }) };
        for(auto& worker:workers) worker.join();
        KirinReferenceGainFacts gain{};
        const auto gainReady=kirin_hypha_analyze_reference_gain(a.data(),b.data(),size_t(rate)*4,uint32_t(rate),uint32_t(channels),&gain);
        check(gainReady==(size_t(rate)*4<=2097152),"existing gain-analysis frame cap remains fail-closed at the highest rate");
        const auto peak=heap(); sampling=false; sampler.join();
        largest=std::max(sampledLive.load(),peak.live)-before.live;
        std::cout<<"RAM "<<rate<<"/"<<channels<<" baseline="<<before.live<<" live="<<peak.live<<" touched-high="<<peak.high<<" sampled-live-delta="<<largest<<std::endl;
        check(largest<=2*16*1024*1024,"two simultaneous POST summaries/queues/restore/encode/gain scratch exceed 2 x 16 MiB");
    }
   #if defined(__APPLE__)
    std::cout<<"Capture RAM: two concurrent POST lifetimes; sampled live heap above preexisting PCM = "<<largest<<" bytes (limit 33554432, 50 us sampling, not RSS); receipt PCM retained = 0\n";
   #else
    std::cout<<"Allocator measurement SKIP on this platform; capacities, restore and serialization verified only\n";
   #endif
    // The original Capture A plan budgets the EBU 3-second history separately. Report its cost.
    const auto ebBefore=heap(); auto* meter=kirin_reference_capture_create(uint32_t(rate),uint32_t(channels));
    check(meter!=nullptr,"maximum-rate EBU meter"); const auto ebAfter=heap();
    std::cout<<"Existing EBU capture meter, separate live heap = "<<(ebAfter.live>ebBefore.live?ebAfter.live-ebBefore.live:0)<<" bytes\n";
    kirin_reference_visual_drop(meter);
}
void testReferenceCaptureMemory()
{
    // Fresh processes prevent historical allocator arenas from accumulating across formats.
    for(int rate:{768000,384000,192000,96000,48000,44100,8000}) for(int channels:{2,1}) {
        juce::ChildProcess child;
        check(child.start(juce::StringArray{juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),
            "--capture-memory-case",juce::String(rate),juce::String(channels)}),"isolated allocator case starts");
        const bool finished=child.waitForProcessToFinish(60000);
        if(!finished) child.kill();
        std::cout<<child.readAllProcessOutput()<<std::flush;
        check(finished && child.getExitCode()==0,"isolated allocator case passes");
    }
}
