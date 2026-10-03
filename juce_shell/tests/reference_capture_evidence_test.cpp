#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"
#include "../src/reference_audition/ReferenceCaptureEvidence.h"
#include <iostream>
void testReferenceCaptureEvidence(const juce::File&);
void testReferenceCaptureEvidence(const juce::File& sandbox)
{
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
    bool observedVersionReceipt=false;
    const auto feed=[&](float gain) {
        juce::AudioBuffer<float> input(2,4800);
        for(int at=0;at<fixture.audio.getNumSamples();at+=4800) {
            const auto unitIndex=size_t(at/48000);
            const bool unitBoundary=((at+4800)%48000)==0;
            const auto before=access->snapshot();
            const auto previousPass=unitBoundary && unitIndex<before.unitPass.size()
                ? before.unitPass[unitIndex] : 0;
            for(int c=0;c<2;++c) input.copyFrom(c,0,fixture.audio,c,at,4800); input.applyGain(gain);
            const auto processedBefore=access->framesProcessed.load(std::memory_order_acquire);
            controller.observeTransport(at,true,true); controller.observeAInput(input,at,true,true,true,1);
            controller.setPresented(true);
            if(access->active.load(std::memory_order_acquire))
                require(wait([&]{return !access->active.load(std::memory_order_acquire)
                    || access->framesProcessed.load(std::memory_order_acquire)>=processedBefore+4800;}),
                    "synthetic host waits for finite Capture A worker capacity");
            else if(unitBoundary && before.held)
                require(wait([&]{const auto s=access->snapshot();return unitIndex<s.unitPass.size()
                    && s.unitPass[unitIndex]>previousPass;}),
                    "synthetic host waits for each held-capture comparison unit");
            else juce::Thread::sleep(12);
            if(unitBoundary) { const auto s=controller.snapshot(); observedVersionReceipt |= s.versionSelection && s.versionSelection->aCaptureAvailable; }
        }
    };
    feed(0.5f); juce::AudioBuffer<float> stopped(2,16); stopped.clear();
    controller.observeAInput(stopped,384000,true,false,true,1);
    require(wait([&]{return !access->active;}),"whole A capture finishes");
    const auto initial=access->snapshot().held;
    require(initial && initial->units.size()==8 && initial->bindings.empty(),"Capture does not need B or fabricate correspondence");
    selection.captureState=controller.savedSettings().captureState; selection.capturedView=true;
    controller.restoreSettings(selection);
    require(controller.savedSettings().captureState==selection.captureState,"configured controller immediately saves pending restoration");
    // 2026-10-04：製品は A を取り込まない。設定（DAW の曲）に入っている取り込みは復元しない（ここで取った A のまま）。
    juce::Thread::sleep(200);
    require(access->snapshot().held && !access->snapshot().held->restored,"a saved Capture in the settings is not restored");
    // 取り込みは製品で使わない（2026-10-04）。保存した取り込みとの照合・V の根拠はここまで。
    std::cout<<"Capture evidence: B-free capture, a saved Capture is not restored PASS\n";

}
