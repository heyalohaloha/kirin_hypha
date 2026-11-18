#pragma once
#include "SpectrumPairedPixelReplay.h"
#include "../src/HyphaSpectrumCadenceProfile.h"
namespace hypha::tests::freq_cadence_test {
using namespace freq_paced;
#if defined(KIRIN_FREQ_PHASE_PROFILE)
inline juce::String displayedHash(const SpectrumComponent& view) {
    juce::MemoryOutputStream bytes;
    for(size_t i=0;i<KIRIN_SPECTRUM_BAND_COUNT;++i) {
        for(float value:{view.displayedPreForTest(i),view.displayedPostForTest(i),view.displayedDeltaForTest(i),
                         view.readoutMidForTest(i),view.readoutSideForTest(i),view.readoutDeltaForTest(i)}) {
            check(std::isfinite(value),"cadence arrays must be finite"); bytes.writeFloat(value);
        }
        bytes.writeByte(view.displayedDeltaValidForTest(i)?1:0);
        bytes.writeByte(view.readoutDeltaValidForTest(i)?1:0);
    }
    return juce::SHA256(bytes.getData(),bytes.getDataSize()).toHexString();
}
inline juce::var countsJson(const freq_cadence::Counts& c) {
    auto v=object();
#define FIELD(name) set(v,#name,static_cast<juce::int64>(c.name))
    FIELD(batchCalls); FIELD(batchSeed); FIELD(batchForwarded); FIELD(batchOldSkip); FIELD(batchReseed);
    FIELD(queueCalls); FIELD(queueIdentical); FIELD(tickCalls); FIELD(curveBranch); FIELD(numericBranch); FIELD(tickRepaint);
#undef FIELD
    return v;
}
inline void run(const Input& input,int width,int every,const juce::File& dir,const juce::String& sourceId) {
    material_cache::Lifetime material; SpectrumComponent view;
    view.setPresentationContext(presentation::forEditor(width,width*2/3)); view.setSize(width,width*2/3);
    view.setAbsoluteObservation(false); view.setLowScope(false); view.setDisplaySettings(display_settings::Values{});
    view.setAverageChoice(SpectrumComponent::Average::live); view.setSignalActive(true); view.setAnimationVisible(false);
    check(!view.frameDriverActiveForTest(),"cadence driver must not attach VBlank");
    auto batch=std::make_unique<KirinSpectrumBatch>(); batch->count=1;
    batch->latest=batch->frames[0]=input.frames.front(); view.setBatch(*batch,1,1);
    const double base=view.curveUpdateTimeForTest(); freq_cadence::reset();
    auto previous=displayedHash(view); int changed=0,unchanged=0,paintCalls=0;
    juce::Image image(juce::Image::ARGB,width,width*2/3,true); juce::Array<juce::var> records;
    const auto prefix=juce::String(width)+"-update-every-"+juce::String(every);
    for(int i=0;i<900;++i) {
        const int inputIndex=every==-1?30:(every<=0?0:(i/every)*every);
        batch->latest=batch->frames[0]=input.frames[static_cast<size_t>(inputIndex)];
        if(every==-2) batch->latest.presentation_end_samples=batch->frames[0].presentation_end_samples=input.frames[static_cast<size_t>(i)].presentation_end_samples;
        view.setBatch(*batch,1,1); view.presentationTickAt(base+static_cast<double>(i)*1000.0/30.0);
        const auto hash=displayedHash(view); const bool differs=hash!=previous;
        changed+=differs; unchanged+=!differs; previous=hash;
        image.clear(image.getBounds(),BG); { juce::Graphics g(image); view.paintEntireComponent(g,true); } ++paintCalls;
        auto row=object(); set(row,"slot",i); set(row,"input_index",inputIndex);
        set(row,"endpoint",static_cast<juce::int64>(view.presentedEndpointForTest()));
        set(row,"display_readout_sha256",hash); set(row,"arrays_changed",differs);
        if(i==0 || i==1 || i==30 || i==899) {
            set(row,"rgba_sha256",freq_pixels::rgbaHash(image));
            freq_pixels::savePng(dir.getChildFile(prefix+"-slot-"+juce::String(i)+".png"),image);
        }
        records.add(row);
    }
    const auto c=freq_cadence::read(); const int forwarded=every==-1?1:(every==-2?899:(every==0?0:899/every));
    check(c.batchCalls==900 && c.tickCalls==900 && paintCalls==900,"cadence incomplete input/tick/paint");
    check(c.batchForwarded==static_cast<uint64_t>(forwarded) && c.batchOldSkip==static_cast<uint64_t>(900-forwarded),"cadence endpoint acceptance mismatch");
    check(c.batchSeed==0 && c.batchReseed==0,"unexpected continuity reset during cadence fixture");
    check(c.queueCalls==c.batchForwarded,"cadence forwarded frame queue mismatch");
    auto result=object(); set(result,"schema","freq-batch-cadence-diagnostic-v1"); set(result,"source_id_caller_supplied",sourceId);
    set(result,"underlying_input_sha256",input.sha256); set(result,"width",width); set(result,"height",width*2/3);
    set(result,"update_every_slots",every); set(result,"nominal_update_hz",every==-2?30:(every<=0?0:30/every));
    set(result,"case",every==-1?"single-step-then-held":(every==-2?"same-values-new-endpoints":"periodic-or-initial-only"));
    set(result,"virtual_tick_hz",30); set(result,"virtual_seconds",30); set(result,"paced_in_real_time",false);
    set(result,"initial_seed_excluded_from_counts",true); set(result,"counts",countsJson(c)); set(result,"fixture_paint_calls",paintCalls);
    set(result,"display_readout_changed_slots",changed); set(result,"display_readout_unchanged_slots",unchanged); set(result,"frames",records);
    set(result,"valid_for_performance_comparison",false);
    set(result,"scope","paired FULL LR LIVE, no interaction; arrays exclude endpoint, history, trails, text, light; equality is NOT cache-safety proof; offscreen fixture paint calls are NOT product repaint rate");
    saveJson(dir.getChildFile(prefix+".json"),result);
    std::cout<<"FREQ_CADENCE width="<<width<<" every="<<every<<" forwarded="<<c.batchForwarded<<" array_changed="<<changed<<" tick_repaint="<<c.tickRepaint<<" painted="<<paintCalls<<std::endl;
}
#endif
inline int main(const juce::File& dir,const juce::String& sourceId) {
#if defined(KIRIN_FREQ_PHASE_PROFILE)
    check(juce::MessageManager::getInstance()->isThisTheMessageThread(),"cadence requires GUI thread");
    check(sourceId.isNotEmpty()&&!dir.exists(),"cadence needs source id and fresh directory");
    check(dir.createDirectory().wasOk(),"cannot create cadence evidence directory");
    const i18n::ScopedLanguage language(i18n::Language::english); const auto input=makeInput();
    for(int width:{300,600,900}) for(int every:{0,3,1,-1,-2}) run(input,width,every,dir,sourceId);
    return 0;
#else
    (void)dir; (void)sourceId; throw std::runtime_error("cadence is diagnostic-profile-only");
#endif
}
}
