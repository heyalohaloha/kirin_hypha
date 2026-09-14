#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "../src/reference_audition/ReferenceVisualObservation.h"
#include "../src/reference_audition/ReferenceVisualAudio.h"
#include "reference_rt_probe.h"
#include <thread>
#include <ctime>

void testReferenceVisual (const juce::File&);
void testReferenceVisual (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("visual"); require (root.createDirectory().wasOk(), "visual fixture directory");
    const auto file = root.getChildFile ("same-song.wav");
    const auto fixture = makeWholeSongFixture (root, file, "recording-visual", "version-visual");
    auto source = std::make_shared<ref::RuntimeSource>();
    source->absolutePath = file.getFullPathName(); source->audio = { 48000, 2, fixture.audio.getNumSamples() };
    const auto rev = fixture.source["file"]["revision"];
    source->revision = { rev["device_id"].toString(), rev["file_id"].toString(), rev["size_bytes"].toString(), rev["mtime_ns"].toString(), rev["ctime_ns"].toString() };
    source->sourceFileSha256 = juce::SHA256 (file).toHexString(); source->sourcePcmSha256 = fixture.pcmHash;
    const auto artifact = fixture.source["measurement"]["detail_artifact"];
    source->measurementArtifact = ref::RuntimeContentReceipt { artifact["relative_path"].toString(), artifact["sha256"].toString(), static_cast<juce::int64> (artifact["bytes"]) };
    ref::RuntimeV2MeasurementRepository measurements (root);
    const auto measurement = measurements.load (*source);
    require (measurement.accepted(), "actual producer waveform/RMS fixture parses");
    ref::VisualBinding map; map.source = source; map.overview = measurement.measurement;
    map.hostRate = 48000; map.channels = 2; map.hostAnchor = 24000; map.aligned = true; map.key = "verified-map-1";
    std::atomic<bool> blockBinding { false }, entered { false };
    auto sharedOwner=std::make_shared<ref::ReferenceAnalysis>();
    ref::VisualObservation observation ([&] {
        if (blockBinding) { entered = true; while (blockBinding) juce::Thread::sleep (1); }
        return map;
    },sharedOwner);
    const auto enqueueInput=[&](const juce::AudioBuffer<float>& input,std::int64_t position,bool valid) {
        if(!valid) return;
        std::array<float,512> pcm{}; const auto epoch=observation.inputGeneration();
        for(int offset=0;offset<input.getNumSamples();offset+=256) {
            const int count=std::min(256,input.getNumSamples()-offset);
            for(int c=0;c<input.getNumChannels();++c) for(int i=0;i<count;++i) pcm[size_t(i*input.getNumChannels()+c)]=input.getSample(c,offset+i);
            observation.enqueue(pcm.data(),count,input.getNumChannels(),position+offset,epoch,1);
        }
    };
    const auto wait = [&] (const auto& predicate) {
        for (int i = 0; i < 300; ++i) { const auto state = observation.snapshot(); if (state && predicate (*state)) return true; juce::Thread::sleep (10); }
        return false;
    };
    require (wait ([] (const auto& state) { return !state.observing; }), "hidden view starts without an analysis owner");
    auto* owner1 = kirin_reference_visual_admission_create(); auto* owner2 = kirin_reference_visual_admission_create();
    require (kirin_reference_visual_admission_set (owner1, true) && kirin_reference_visual_admission_set (owner2, true), "two analysis owners available");
    observation.setPresented (true); juce::Thread::sleep (150);
    require (observation.snapshot() && !observation.snapshot()->observing, "third observation may not create a hidden analysis slot");
    kirin_reference_visual_admission_set (owner2, false);
    require (wait ([] (const auto& state) { return state.observing; }), "view uses the released analysis slot");
    const auto previousPass = observation.snapshot()->pass;
    observation.pauseAdmission();
    ref::ReferenceAnalysis::Lease audition;
    for(int i=0;i<100 && !audition;++i) { audition=sharedOwner->acquire(); if(!audition) juce::Thread::sleep(5); }
    require(bool(audition),"audition shares the same concrete engine owner");
    observation.resumeObservation();
    require (wait ([&] (const auto& state) { return state.observing && state.pass > previousPass; }), "observation can share admitted audition work");
    auto* excess = kirin_reference_visual_admission_create();
    require (!kirin_reference_visual_admission_set (excess, true), "borrowed observation still permits only two total owners");
    kirin_reference_visual_admission_drop (excess);
    juce::AudioBuffer<float> input (2, 4800);
    const auto feed = [&] (int bin) {
        for (int c = 0; c < 2; ++c) input.copyFrom (c, 0, fixture.audio, c, bin * 4800, 4800);
        beginReferenceRtProbe(); enqueueInput (input, 24000 + bin * 4800, true);
        require (endReferenceRtProbe() == 0, "display copy allocates zero times in callback");
        require (std::memcmp (input.getReadPointer (1,32), fixture.audio.getReadPointer (1,bin*4800+32), sizeof(float)) == 0, "observation preserves original A bits");
        juce::Thread::sleep (10);
    };
    const auto measurementCpu = std::clock();
    for (int i = 0; i < 40; ++i) feed (i);
    require (wait ([] (const auto& state) { return state.bins.size() == 80 && state.bins[39].pass != 0; }), "same-song bins are placed at the verified source position");
    std::cout << "Reference A/B display CPU at 48k stereo=" << 100.0 * double(std::clock()-measurementCpu) / CLOCKS_PER_SEC / 4.0 << "% of one core (4s fixture, includes test copy)\n";
    const auto measured = observation.snapshot();
    for (int i = 0; i < 40; ++i)
    {
        const auto& bin = measured->bins[size_t (i)];
        require (bin.a.frames == 4800 && bin.b.frames == 4800, "same complete A/B bin windows");
        require (std::abs (bin.a.peak[0] - bin.b.peak[0]) < 1e-8 && std::abs (bin.a.rms[1]-bin.b.rms[1]) < 1e-8, "same PCM peak and stereo RMS agree");
        require (std::abs (bin.a.crest_db-bin.b.crest_db) < 0.001, "same true-peak/RMS Crest");
        if (i < 29) require (std::isnan (bin.a.short_lufs), "short-term values wait for three continuous seconds");
        else require (std::abs (bin.a.short_lufs-bin.b.short_lufs) < 0.001, "same three-second endpoint loudness");
    }
    require (measured->bins[45].pass == 0, "unvisited A is missing, never silent or copied B");
    feed (60); feed (61);
    require (wait ([] (const auto& state) { return state.bins[61].pass == state.pass; }), "seek publishes a new pass");
    auto sought = observation.snapshot();
    require (sought->bins[10].pass != sought->pass && std::isnan (sought->bins[61].a.short_lufs), "history is distinct and windows do not cross a seek");
    std::vector<double> routing;
    std::array<float,9600> routePcm{};
    for(int c=0;c<2;++c) for(int f=0;f<4800;++f) routePcm[size_t(f*2+c)]=input.getSample(c,f);
    for(int sample=0;sample<40;++sample)
    {
        for(int i=0;i<400 && observation.pendingInput();++i) juce::Thread::sleep(1);
        require(!observation.pendingInput(),"routing benchmark starts with queue capacity");
        const auto start=juce::Time::getMillisecondCounterHiRes(),epoch=double(observation.inputGeneration());
        for(int offset=0;offset<4800;offset+=256) observation.enqueue(routePcm.data()+size_t(offset*2),std::min(256,4800-offset),2,24000+offset,std::uint64_t(epoch),1);
        routing.push_back(juce::Time::getMillisecondCounterHiRes()-start);
    }
    std::sort(routing.begin(),routing.end());
    std::cout<<"Shared routing p95 per 100ms="<<routing[38]<<" ms; queues="
        <<ref::ACaptureSession::inputQueueBytes()+ref::VisualObservation::inputQueueBytes()<<" bytes; ingress capacity="<<ref::ACaptureSession::inputQueueFrames()<<" frames\n";
    require(routing[38]<0.1,"non-RT routing stays below 0.1 ms per 100 ms of stereo input");
    require(ref::ACaptureSession::inputQueueBytes()+ref::VisualObservation::inputQueueBytes()<=2*1024*1024,"both bounded queues fit 2 MiB");
    blockBinding = true;
    for (int i=0; i<200 && !entered; ++i) juce::Thread::sleep (5);
    require (entered, "simulate stalled non-RT source preparation");
    {
        ref::ACaptureSession capture([](bool){return true;},{},sharedOwner,&observation);
        capture.configure("blocked-b-capture",48000,2); require(capture.access->request(ref::ACaptureAccess::start),"Capture admitted while B worker is stalled");
        for(int i=0;i<600 && !capture.access->active;++i) juce::Thread::sleep(5);
        require(capture.access->active,"Capture uses its own finite worker");
        for(int i=0;i<40;++i) {
            for(int c=0;c<2;++c) input.copyFrom(c,0,fixture.audio,c,i*4800,4800);
            beginReferenceRtProbe(); capture.observe(input,24000+i*4800,true,true,true,1);
            require(endReferenceRtProbe()==0,"stalled B never introduces RT allocation"); juce::Thread::sleep(10);
        }
        capture.observe(input,216000,true,false,true,1);
        for(int i=0;i<600 && capture.access->busy();++i) juce::Thread::sleep(5);
        require(capture.access->snapshot().held && capture.access->snapshot().held->frames==192000,"stalled B and overflowing LIVE queue lose zero Capture frames");
        capture.setPresented(true);
        for(int i=0;i<600 && !capture.access->analysisAvailable;++i) juce::Thread::sleep(5);
        for(int i=0;i<12;++i) {
            for(int c=0;c<2;++c) input.copyFrom(c,0,fixture.audio,c,i*4800,4800); input.applyGain(0.5f);
            capture.observe(input,24000+i*4800,true,true,true,1); juce::Thread::sleep(10);
        }
        bool changed=false; for(int i=0;i<600 && !changed;++i) { const auto state=capture.access->snapshot(); changed=!state.unitStatus.empty() && state.unitStatus.front()==2; if(!changed) juce::Thread::sleep(5); }
        require(changed,"stalled B cannot stop same-position A change detection");
    }
    beginReferenceRtProbe();
    for (int i=0; i<100; ++i) enqueueInput (input, 24000 + i*4800, true);
    require (endReferenceRtProbe() == 0, "a full display queue stays bounded without RT allocation");
    audition.reset();
    const auto before = juce::Time::getMillisecondCounterHiRes(); observation.pauseAdmission();
    require (juce::Time::getMillisecondCounterHiRes()-before < 100, "admission release never waits behind blocked source work");
    require(!kirin_reference_visual_admission_set(owner2,true),"retiring source job still owns its physical slot after demand cancellation");
    observation.resumeObservation(); observation.setPresented (false); blockBinding = false;
    bool retired=false; for(int i=0;i<100 && !retired;++i) { retired=kirin_reference_visual_admission_set(owner2,true); if(!retired) juce::Thread::sleep(5); }
    require(retired,"physical slot becomes available only after the delayed job returns");
    audition.reset();
    kirin_reference_visual_admission_drop (owner1); kirin_reference_visual_admission_drop (owner2);
    // The display converter is the very same implementation used by audible pages.
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    ref::AudioPages pages;
    source->sourceKind = "work_version";
    require (pages.open (*source, 44100, 2, true).isEmpty(), "non-integer rate audition source");
    pages.request (1777); pages.service();
    juce::AudioBuffer<float> audible (2,128), display, scratch;
    require (pages.render (audible, 1777, 1.0f), "audible resampled B pages ready");
    require (ref::readReferenceVisualAudio (*reader,1777,128,44100,2,display,scratch), "converted display data");
    for (int c=0;c<2;++c) for (int i=0;i<128;++i)
        require (std::memcmp (audible.getReadPointer (c,i), display.getReadPointer (c,i), sizeof(float)) == 0, "display PCM equals audition PCM at non-integer phase");
    observation.setPresented (true);
    require (wait ([] (const auto& state) { return state.observing; }), "reopen can reacquire after handoff");
    require (file.setLastModificationTime (juce::Time::getCurrentTime()+juce::RelativeTime::seconds (5)), "source revision change fixture");
    require (wait ([] (const auto& state) { return !state.binding.aligned && !state.observing; }), "changed source invalidates paired observation and releases its slot");
    std::int64_t mapped = 0;
    auto preroll = map; preroll.hostAnchor = -24000;
    require (preroll.mapPosition (-24000,mapped) && mapped == 0, "negative host preroll can map to source frame zero");
    require (!preroll.mapPosition (std::numeric_limits<std::int64_t>::max(),mapped), "extreme host positions fail closed without signed overflow");
    ref::VisualTimeline grid; grid.binding = map; grid.binding.hostRate = 44100; grid.hop = 4800;
    require (grid.boundary (1999) == 1999LL*4410 && grid.boundary (1) == 4410, "rational bin mapping never accumulates rounding error");
    grid.hop = 4096;
    require (grid.boundary (1) == 3764 && ref::VisualTimeline::outputSample (7,48000,44100)==7, "fractional boundaries and final frames match the audible ceil convention");
    std::cout << "Reference visual: coverage, gain/window contract, RT copy, 2 slots, release and resampler parity passed\n";
}

void testReferenceVisualIntegration (ref::ReferenceComparisonController&, const juce::AudioBuffer<float>&);
void testReferenceVisualIntegration (ref::ReferenceComparisonController& controller, const juce::AudioBuffer<float>& song)
{
    controller.setPresented (true);
    // Allow the visible, calibrated Version binding to publish before the first callback.
    juce::Thread::sleep (150);
    for (int position=0; position+4800<=song.getNumSamples(); position+=4800)
    {
        juce::AudioBuffer<float> block (2,4800);
        for (int c=0;c<2;++c) block.copyFrom (c,0,song,c,position,4800);
        controller.observeTransport (position,true,true);
        beginReferenceRtProbe();
        controller.observeAInput (block,position,true,true,true);
        const bool rendered = controller.renderSelectedB (block,position,true,true,true);
        require (endReferenceRtProbe()==0 && !rendered, "whole-song display adds no RT heap operations or implicit B switch");
        juce::Thread::sleep (10);
    }
    size_t covered = 0;
    for (int i=0;i<300;++i)
    {
        const auto state = controller.snapshot(); covered = 0;
        if (state.visualTimeline) for (const auto& bin : state.visualTimeline->bins) if (bin.pass) ++covered;
        if (covered >= 75) break;
        juce::Thread::sleep (10);
    }
    std::cout << "Reference complete runtime observed " << covered << " / 80 waveform bins\n";
    require (covered >= 75, "verified library and live A feed the complete comparison view");
}
