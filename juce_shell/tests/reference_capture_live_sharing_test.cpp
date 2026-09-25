#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"
#include "../src/reference_audition/ReferenceCaptureEvidence.h"
#include <iostream>
void testCaptureLiveSharing();
static void captureLiveSharing(const juce::File& sandbox);
void testCaptureLiveSharing() {
    const auto sandbox=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hypha-b885-review-"+juce::Uuid().toString());
    require(sandbox.createDirectory(),"review sandbox");
    // The controller and its workers end with captureLiveSharing(). Windows cannot delete a file a
    // worker still holds open, so the sandbox is removed only after that, as in the other suites.
    captureLiveSharing(sandbox);
    require(sandbox.deleteRecursively(),"review sandbox cleanup");
}
static void captureLiveSharing(const juce::File& sandbox) {
    const auto root=sandbox.getChildFile("capture-evidence"); require(root.createDirectory(),"capture evidence fixture");
    const auto file=root.getChildFile("version.wav");
    const juce::String recording="22222222-2222-4222-8222-222222222222",version="33333333-3333-4333-8333-333333333333";
    const auto fixture=makeWholeSongFixture(root,file,recording,version);
    auto preset=bindRuntimeV2WorkVersionPresetToSource("88888888-8888-4888-8888-888888888888","99999999-9999-4999-8999-999999999999",
        fixture.receipt,juce::SHA256(file).toHexString(),fixture.pcmHash,recording,version,fixture.audio.getNumSamples());
    auto* p=preset.getDynamicObject(); p->setProperty("format","kirin_hypha_reference_library_preset"); p->setProperty("version","1.0");
    p->removeProperty("work_id"); p->removeProperty("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty("preparation_status","prepared");
    libraryManifest(root,preset,1);
    require(writeJson(root.getChildFile("library/manifest.json"),independentLibraryManifest(root,preset,2)),"independent Version catalog");
    ref::ReferenceComparisonSettings selection; selection.viewedSlot=1;
    selection.version={preset["source_template_artifact"]["preset_id"].toString(),preset["checks"][0]["check_id"].toString(),
        preset["checks"][0]["candidates"][0]["candidate_id"].toString(),preset["checks"][0]["candidates"][0]["default_cue_id"].toString()};
    ref::ReferenceComparisonController controller(root,[](bool){return true;},[](bool){return true;});
    controller.configure({"capture-evidence-post",{},42,true},48000,2); controller.setPresented(true);
    const auto access=controller.snapshot().captureAccess;
    require(access && access->request(ref::ACaptureAccess::start),"Capture A begins without selected B");
    const auto wait=[](const auto& test){ for(int i=0;i<800;++i){ if(test())return true;juce::Thread::sleep(5); }return false; };
    require(wait([&]{return access->active.load();}),"Capture A admitted");
    const auto feed=[&](float gain, bool paceLive=false) {
        if(paceLive)
            require(wait([&]{const auto s=controller.snapshot();return s.visualTimeline
                && s.visualTimeline->observing && s.visualTimeline->pairedObserving;}),
                "LIVE comparison is observing before the synthetic host runs");
        juce::AudioBuffer<float> input(2,4800);
        for(int at=0;at<fixture.audio.getNumSamples();at+=4800) {
            for(int c=0;c<2;++c) input.copyFrom(c,0,fixture.audio,c,at,4800); input.applyGain(gain);
            const auto processedBefore=access->framesProcessed.load(std::memory_order_acquire);
            controller.observeTransport(at,true,true); controller.observeAInput(input,at,true,true,true,1);
            controller.setPresented(true);
            if(access->active.load(std::memory_order_acquire))
                require(wait([&]{return !access->active.load(std::memory_order_acquire)
                    || access->framesProcessed.load(std::memory_order_acquire)>=processedBefore+4800;}),
                    "synthetic host waits for finite Capture A worker capacity");
            else if(paceLive)
                require(wait([&,index=size_t(at/4800)]{const auto s=controller.snapshot();return s.visualTimeline
                    && index<s.visualTimeline->bins.size() && s.visualTimeline->bins[index].pass!=0;}),
                    "synthetic host waits for the LIVE comparison worker");
            else juce::Thread::sleep(12);
        }
    };
    feed(0.5f); juce::AudioBuffer<float> stopped(2,16); stopped.clear();
    controller.observeAInput(stopped,384000,true,false,true,1);
    require(wait([&]{const auto state=access->snapshot();return !access->active && state.held;}),
        "whole A capture publishes its terminal held snapshot");
    const auto initial=access->snapshot().held;
    require(initial && initial->units.size()==8 && initial->bindings.empty(),"Capture does not need B or fabricate correspondence");
    selection.captureState=controller.savedSettings().captureState; selection.capturedView=true;
    controller.restoreSettings(selection);
    require(controller.savedSettings().captureState==selection.captureState,"configured controller immediately saves pending restoration");
    require(wait([&]{const auto s=access->snapshot();return s.held && s.held->restored;}),"Capture restored independently of B");
    for(int pass=0;pass<8 && access->snapshot().held->bindings.empty();++pass) feed(0.5f);
    require(wait([&]{const auto s=access->snapshot();return s.held && !s.held->bindings.empty();}),"later B gains a receipt only through matching four-unit A evidence");
    const auto bound=access->snapshot().held; const auto proof=bound->bindings.front();
    require(proof.valid() && std::abs(proof.displayGainDb+6.0205999)<0.01,"captured B uses fixed paired-block gain, not whole-song integrated difference");
    require(proof.hostAnchor==proof.sourceAnchor && proof.probeEnd-proof.probeStart==192000,"historical source position error is zero samples");

    access->capturedView=false; controller.setPresented(true);
    for(int pass=0;pass<3;++pass) feed(0.5f,true);
    const auto count=[](const auto& s){size_t n=0;if(s.visualTimeline)for(const auto& b:s.visualTimeline->bins) if(b.a.frames && b.pass)++n;return n;};
    auto before=controller.snapshot();
    std::cout << "LIVE with held A: timeline=" << bool(before.visualTimeline) << " captured=" << (before.visualTimeline && bool(before.visualTimeline->capture)) << " observing=" << (before.visualTimeline && before.visualTimeline->observing) << " measured_bins=" << count(before) << " revisit_units=" << access->snapshot().unitStatus.size() << std::endl;
    require(count(before)==80,"LIVE receives every fixture bin while captured A is retained");
    auto* spare=kirin_reference_visual_admission_create();
    const bool slotWithHeld=kirin_reference_visual_admission_set(spare,true);
    std::cout << "second POST admission while held A + LIVE=" << slotWithHeld << std::endl;
    require(slotWithHeld,"held A plus LIVE leave the second POST analysis slot free");
    kirin_reference_visual_admission_drop(spare);
    selection.captureState={}; selection.capturedView=false; controller.restoreSettings(selection);
    require(wait([&]{return !access->snapshot().held;}),"capture cleared for control");
    controller.setPresented(true);
    for(int pass=0;pass<3;++pass) feed(0.5f,true);
    const auto after=controller.snapshot();
    std::cout << "LIVE without held A: timeline=" << bool(after.visualTimeline) << " observing=" << (after.visualTimeline && after.visualTimeline->observing) << " measured_bins=" << count(after) << std::endl;
    spare=kirin_reference_visual_admission_create();
    std::cout << "second POST admission without held A=" << kirin_reference_visual_admission_set(spare,true) << std::endl;
    kirin_reference_visual_admission_drop(spare);
}
