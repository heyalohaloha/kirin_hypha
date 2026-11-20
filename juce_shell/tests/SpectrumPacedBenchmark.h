#pragma once
#include "CompilerVersionString.h"
// Paired full-component native-image offscreen workload, not DAW/VBlank/dirty-region CPU.
#include "SpectrumPacedBenchmarkSupport.h"
#include "SpectrumPhaseProfileCollector.h"
#include "SpectrumPerformanceFixture.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaMaterialCache.h"
#include "../src/HyphaLanguage.h"
#include <iostream>
namespace hypha::tests::freq_paced
{
constexpr int fps=30,warmupFrames=60,measuredFrames=900;
struct Input { std::vector<KirinSpectrumView> frames; juce::String sha256; };
inline Input makeInput() {
    Input out; out.frames.reserve(warmupFrames+measuredFrames); juce::MemoryOutputStream canonical;
    canonical.writeString("dense-stereo-paired-v1;zero-initialized-spectrumPerformanceFixture;48k;4096;8192;LR;256");
    for(int frame=0;frame<warmupFrames+measuredFrames;++frame) {
        auto value=spectrumPerformanceFixture(); value.presentation_end_samples=48000+static_cast<int64_t>(frame)*1600;
        canonical.writeInt64(value.presentation_end_samples);
        canonical.writeInt(static_cast<int>(value.status)); canonical.writeInt(value.has_data); canonical.writeInt(value.post_has_data);
        canonical.writeInt(value.channel_mode); canonical.writeInt(value.channels); canonical.writeInt(value.analysis_view);
        canonical.writeInt(value.sample_rate); canonical.writeInt(value.aperture_samples); canonical.writeInt(value.fft_size);
        canonical.writeFloat(value.approximate_below_hz); canonical.writeFloat(value.min_hz); canonical.writeFloat(value.max_hz);
        // Fixed-index modulation; no wall clock, random state, FFT or production measurement changes.
        const float shift=3.0f*std::sin(frame*.1f),delta=1.5f*std::sin(frame*.07f);
        for(size_t band=0;band<KIRIN_SPECTRUM_BAND_COUNT;++band) {
            value.pre_dbfs[band]+=shift; value.display_db[band]+=delta;
            value.post_dbfs[band]=value.pre_dbfs[band]+value.display_db[band];
            check(std::isfinite(value.pre_dbfs[band]) && std::isfinite(value.post_dbfs[band]) && std::isfinite(value.display_db[band]),"fixture must be finite");
            canonical.writeFloat(value.pre_dbfs[band]); canonical.writeFloat(value.post_dbfs[band]); canonical.writeFloat(value.display_db[band]);
        }
        out.frames.push_back(value);
    }
    // Hash canonical field bytes, never C struct padding. Fixed schema fields are recorded above.
    out.sha256=juce::SHA256(canonical.getData(),canonical.getDataSize()).toHexString(); return out;
}
struct Slot {
    int index=0; bool dropped=false,overrun=false; int64_t endpoint=0;
    double nominalMs=0,startMs=0,finishMs=0,lateMs=0,workMs=0,paintMs=0,tickMs=0,queueMs=0;
};
inline juce::var slotJson(const Slot& s) {
    auto out=object(); set(out,"index",s.index); set(out,"endpoint",static_cast<juce::int64>(s.endpoint));
    set(out,"dropped",s.dropped); set(out,"overrun",s.overrun); set(out,"nominal_ms",s.nominalMs);
    set(out,"start_ms",s.startMs); set(out,"finish_ms",s.finishMs); set(out,"late_ms",s.lateMs);
    if(!s.dropped) { set(out,"work_ms",s.workMs); set(out,"paint_ms",s.paintMs); set(out,"tick_ms",s.tickMs); set(out,"queue_ms",s.queueMs); }
    return out;
}
inline juce::var run(const Input& input,int width,int repetition,const juce::String& sourceId) {
    check(juce::MessageManager::getInstance()->isThisTheMessageThread(),"benchmark must run on GUI/message thread");
    SpectrumComponent view; view.setPresentationContext(presentation::forEditor(width,width*2/3));
    view.setSize(width,width*2/3); view.setAbsoluteObservation(false); view.setLowScope(false);
    view.setDisplaySettings(display_settings::Values{});
    view.setAverageChoice(SpectrumComponent::Average::live); view.setSignalActive(true); view.setAnimationVisible(false);
    check(!view.frameDriverActiveForTest(),"manual benchmark must not have a VBlank attachment");
    juce::Image image(juce::Image::ARGB,width,width*2/3,true);
    auto present=[&](const KirinSpectrumView& frame) {
        view.queueSnapshot(frame,1); view.presentationTickAt(juce::Time::getMillisecondCounterHiRes());
        image.clear(image.getBounds(),BG); juce::Graphics g(image); view.paintEntireComponent(g,true);
    };
    const auto period=std::chrono::duration<double>(1.0/fps);
    const auto warm=Clock::now();
    for(int i=0;i<warmupFrames;++i) {
        std::this_thread::sleep_until(warm+std::chrono::duration_cast<Clock::duration>(period*i));
        present(input.frames[static_cast<size_t>(i)]);
    }
    std::this_thread::sleep_until(warm+std::chrono::seconds(2));
    std::vector<Slot> slots; slots.reserve(measuredFrames);
    ThreadCpu cpu; const auto backdrop=view.backdropBuildsForTest();
    const auto paths=view.curvePathAllocationsForTest(); const auto repaints=view.tickRepaintsForTest();
    startPhaseProfile();
    const double cpuBegin=cpu.seconds(); const auto started=Clock::now();
    for(int i=0;i<measuredFrames;++i) {
        const auto due=started+std::chrono::duration_cast<Clock::duration>(period*i);
        const auto next=started+std::chrono::duration_cast<Clock::duration>(period*(i+1));
        std::this_thread::sleep_until(due); const auto begin=Clock::now();
        const auto& frame=input.frames[static_cast<size_t>(warmupFrames+i)];
        Slot slot; slot.index=i; slot.endpoint=frame.presentation_end_samples;
        slot.nominalMs=ms(due-started); slot.startMs=ms(begin-started); slot.lateMs=ms(begin-due);
        // No burst catch-up: missed slots remain recorded with their original frame indices.
        if(begin>=next) { slot.dropped=true; slot.finishMs=slot.startMs; slots.push_back(slot); continue; }
        view.queueSnapshot(frame,1); const auto queued=Clock::now();
        view.presentationTickAt(juce::Time::getMillisecondCounterHiRes()); const auto ticked=Clock::now();
        image.clear(image.getBounds(),BG); juce::Graphics graphics(image); const auto paintBegin=Clock::now();
        view.paintEntireComponent(graphics,true); const auto finished=Clock::now();
        slot.finishMs=ms(finished-started); slot.queueMs=ms(queued-begin); slot.tickMs=ms(ticked-queued);
        slot.paintMs=ms(finished-paintBegin); slot.workMs=ms(finished-begin); slot.overrun=finished>next;
        slots.push_back(slot);
    }
    std::this_thread::sleep_until(started+std::chrono::seconds(30));
    const auto stopped=Clock::now(); const double cpuEnd=cpu.seconds();
    stopPhaseProfile();
    check(!view.frameDriverActiveForTest(),"manual benchmark unexpectedly attached VBlank");
    const double wall=ms(stopped-started)/1000.0,cpuSeconds=cpuEnd-cpuBegin;
    check(wall>=30.0 && cpuSeconds>=0 && std::isfinite(cpuSeconds),"invalid measured clock interval");
    std::vector<double> work,paint,queue,tick,late; juce::Array<juce::var> records;
    int drops=0,overruns=0;
    for(const auto& slot:slots) {
        records.add(slotJson(slot)); late.push_back(slot.lateMs);
        if(slot.dropped) {++drops;continue;} overruns+=slot.overrun;
        work.push_back(slot.workMs); paint.push_back(slot.paintMs); queue.push_back(slot.queueMs); tick.push_back(slot.tickMs);
    }
    auto out=object(); set(out,"schema","hypha-freq-paced-v1"); set(out,"source_id_caller_supplied",sourceId);
    set(out,"compiler",hypha::tests::compilerVersionString()); set(out,"os",juce::SystemStats::getOperatingSystemName());
    set(out,"cpu_model",juce::SystemStats::getCpuModel()); set(out,"juce_version",juce::SystemStats::getJUCEVersion());
    set(out,"fixture_sha256",input.sha256); set(out,"width",width); set(out,"height",width*2/3); set(out,"run",repetition);
    set(out,"mode","paired-LR-full-scope-average-live"); set(out,"render_scale",1); set(out,"language","en");
    set(out,"renderer","ARGB NativeImageType paintEntireComponent every non-dropped slot; macOS CoreGraphics bitmap; dirty-area benefit NOT measured");
    set(out,"clock","steady wall; real JUCE tick; mach current-thread user+system CPU");
    set(out,"nominal_fps",fps); set(out,"warmup_frames",warmupFrames); set(out,"nominal_slots",measuredFrames);
    set(out,"rendered",static_cast<int>(work.size())); set(out,"dropped",drops); set(out,"overruns",overruns);
    set(out,"wall_seconds",wall); set(out,"thread_cpu_seconds",cpuSeconds); set(out,"cpu_ms_per_second",cpuSeconds*1000.0/wall);
    set(out,"one_core_percent",cpuSeconds*100.0/wall); set(out,"actual_fps",work.size()/wall);
    const bool pacingValid=drops==0 && overruns==0 && work.size()==measuredFrames;
    set(out,"profile",phaseProfileBuild); set(out,"pacing_valid",pacingValid);
    set(out,"valid_for_performance_comparison",!phaseProfileBuild && pacingValid);
    exportPhaseProfile(out,static_cast<int>(work.size()));
    set(out,"work_ms",distribution(work)); set(out,"paint_ms",distribution(paint)); set(out,"queue_ms",distribution(queue));
    set(out,"tick_ms",distribution(tick)); set(out,"start_lateness_ms",distribution(late));
    set(out,"backdrop_builds",static_cast<juce::int64>(view.backdropBuildsForTest()-backdrop));
    set(out,"curve_path_allocations",static_cast<juce::int64>(view.curvePathAllocationsForTest()-paths));
    set(out,"tick_repaints",static_cast<juce::int64>(view.tickRepaintsForTest()-repaints));
    juce::Array<juce::var> settings; for(auto value:view.displaySettingsForTest().selected) settings.add(value);
    set(out,"display_settings",settings); set(out,"slots",records); return out;
}
inline void annotateAwakeSeries(juce::var& result) {
    set(result,"benchmark_series","awake-process-activity-v1");
    set(result,"app_nap_inhibition_requested",true);
    set(result,"app_nap_inhibition_api","juce::ScopedLowPowerModeDisabler");
    set(result,"app_nap_activity_option","NSActivityUserInitiatedAllowingIdleSystemSleep");
    set(result,"app_nap_inhibition_effective_verified",false);
    set(result,"comparison_scope","same awake-process-activity-v1 series only; never mix with default paced/profile runs");
}
inline int main(const juce::File& directory,const juce::String& sourceId,bool awakeRequested=false) {
    check(!awakeRequested || !phaseProfileBuild,"awake benchmark requires a non-instrumented binary");
    check(sourceId.isNotEmpty(),"source ID required"); check(!directory.exists(),"use a new benchmark directory");
    check(directory.createDirectory().wasOk(),"cannot create benchmark output directory");
    const i18n::ScopedLanguage english(i18n::Language::english); material_cache::Lifetime material;
    const auto input=makeInput(); auto summary=object(); juce::Array<juce::var> sizeResults;
    bool allValid=true;
    for(int width:{300,600,900}) {
        std::vector<double> cpuValues; bool sizeValid=true;
        for(int repetition=1;repetition<=3;++repetition) {
            auto result=run(input,width,repetition,sourceId);
            if(awakeRequested) annotateAwakeSeries(result);
            saveJson(directory.getChildFile(juce::String(width)+"-run-"+juce::String(repetition)+".json"),result);
            cpuValues.push_back(static_cast<double>(result.getProperty("cpu_ms_per_second",0.0)));
            sizeValid=static_cast<bool>(result.getProperty("pacing_valid",false)) && sizeValid;
            std::cout<<"FREQ_PACED width="<<width<<" run="<<repetition<<" cpu_ms_per_s="<<cpuValues.back()<<" valid="<<sizeValid<<std::endl;
        }
        auto size=object(); set(size,"width",width); set(size,"cpu_ms_per_second",distribution(cpuValues));
        set(size,"pacing_valid",sizeValid); set(size,"valid",!phaseProfileBuild && sizeValid); sizeResults.add(size); allValid=allValid&&sizeValid;
    }
    set(summary,"sizes",sizeResults); set(summary,"all_runs_valid",!phaseProfileBuild && allValid); set(summary,"all_runs_pacing_valid",allValid);
    set(summary,"profile",phaseProfileBuild); set(summary,"valid_for_performance_comparison",!phaseProfileBuild && allValid); set(summary,"fixture_sha256",input.sha256);
    set(summary,"claim",phaseProfileBuild ? "instrumented wall-phase diagnostic; CPU performance comparison INVALID"
        : "measurement only; no baseline reduction or pixel-equivalence verdict");
    if(awakeRequested) annotateAwakeSeries(summary);
    saveJson(directory.getChildFile("summary.json"),summary); return allValid?0:2;
}
}
