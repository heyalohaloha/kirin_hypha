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
    const auto feed=[&](float gain) {
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
            else juce::Thread::sleep(12);
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
    require(wait([&]{const auto s=access->snapshot();return s.held && s.held->restored;}),"Capture restored independently of B");
    for(int pass=0;pass<8 && access->snapshot().held->bindings.empty();++pass) feed(0.5f);
    require(wait([&]{const auto s=access->snapshot();return s.held && !s.held->bindings.empty();}),"later B gains a receipt only through matching four-unit A evidence");
    const auto bound=access->snapshot().held; const auto proof=bound->bindings.front();
    require(proof.valid() && std::abs(proof.displayGainDb+6.0205999)<0.01,"captured B uses fixed paired-block gain, not whole-song integrated difference");
    require(proof.hostAnchor==proof.sourceAnchor && proof.probeEnd-proof.probeStart==192000,"historical source position error is zero samples");
    ref::RuntimeACaptureAudio probe; probe.startSample=0; probe.sampleRateHz=48000; probe.channels=2; probe.frameCount=192000;
    for(int i=0;i<192000;++i) for(int c=0;c<2;++c) probe.interleaved.push_back(fixture.audio.getSample(c,i)*0.5f);
    require(ref::captureProbeMatches(initial->units,0,48000,2,probe),"four exact complete units prove captured probe identity");
    probe.startSample=1; require(!ref::captureProbeMatches(initial->units,0,48000,2,probe),"one-sample moved probe is not padded to unit boundaries");
    probe.startSample=48000; require(!ref::captureProbeMatches(initial->units,0,48000,2,probe),"identical PCM moved one second cannot reuse another captured range");
    probe.startSample=0; probe.sampleRateHz=44100; require(!ref::captureProbeMatches(initial->units,0,48000,2,probe),"sample rate mismatch cannot create identity");
    auto repeated=initial->units; repeated.insert(repeated.end(),initial->units.begin(),initial->units.begin()+4);
    require(!ref::captureUniqueSequence(repeated,0),"repeated four-second sequence cannot anchor a restored timeline");
    const auto encoded=controller.savedSettings().captureState;
    const auto decoded=ref::decodeACapture(encoded);
    require(decoded && decoded->bindings.size()==1 && std::abs(decoded->bindings.front().displayGainDb-proof.displayGainDb)<1e-9,"position and fixed gain persist together");
    for(int pass=0;pass<2;++pass) feed(1.0f);
    const auto after=access->snapshot();
    require(after.held->bindings.front().displayGainDb<=proof.displayGainDb && after.held->bindings.front().displayGainDb>=proof.displayGainDb,"current A edits cannot rewrite historical gain");
    require(std::find(after.unitStatus.begin(),after.unitStatus.end(),std::uint8_t(2))!=after.unitStatus.end(),"changed A notifies after restore timing confirmation");
    require(after.held->id==initial->id && after.held->units.size()==initial->units.size(),"current edits never alter captured A");
    std::cout<<"Capture evidence: B-free capture, later B, exact replay, fixed "<<proof.displayGainDb<<" dB, persistence, current A edits PASS\n";
}
