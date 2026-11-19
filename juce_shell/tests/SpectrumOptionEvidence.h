#pragma once
// Decision material only: alternate presentation rates, never a shipping setting.
#include "SpectrumPairedPixelReplay.h"
namespace hypha::tests::freq_options {
using namespace freq_paced;
inline void prepare(SpectrumComponent& view,int width) {
    view.setPresentationContext(presentation::forEditor(width,width*2/3));view.setSize(width,width*2/3);
    view.setAbsoluteObservation(false);view.setLowScope(false);view.setDisplaySettings(display_settings::Values{});
    view.setAverageChoice(SpectrumComponent::Average::live);view.setSignalActive(true);view.setAnimationVisible(false);
    check(!view.frameDriverActiveForTest(),"options must use explicit test ticks, no VBlank");
}
inline void paint(SpectrumComponent& view,juce::Image& image) {
    image.clear(image.getBounds(),BG);juce::Graphics g(image);view.paintEntireComponent(g,true);
}
inline juce::var cpuRun(int width,int rate,int repetition,const juce::String& source,
        display_settings::Values settings = {},
        spectrum_average::Choice average = spectrum_average::Choice::live, int durationSeconds = 30) {
    check(durationSeconds >= 1 && durationSeconds <= 30, "CPU duration must be1..30seconds");
    check(rate == 15 || rate == 20 || rate == 30, "unsupported presentation rate");
    SpectrumComponent view;prepare(view,width);
    view.setDisplaySettings(settings); view.setAverageChoice(average);
    juce::Image image(juce::Image::ARGB,width,width*2/3,true);
    const auto input=makeInput();const int paintStep=60/rate;
    const auto period=std::chrono::duration<double>(1.0/60);const auto warm=Clock::now();
    for(int i=0;i<120;++i) {
        if(i%2 && i%paintStep) continue;
        std::this_thread::sleep_until(warm+std::chrono::duration_cast<Clock::duration>(i*period));
        if(i%2==0) view.queueSnapshot(input.frames[static_cast<size_t>(i/2)],1);
        if(i%paintStep==0){view.presentationTickAt(juce::Time::getMillisecondCounterHiRes());paint(view,image);}
    }
    std::this_thread::sleep_until(warm+std::chrono::seconds(2));
    std::vector<Slot> slots;slots.reserve(durationSeconds*rate);ThreadCpu cpu;int missedInput=0;
    const auto cpuBegin=cpu.seconds();const auto begin=Clock::now();
    for(int i=0;i<durationSeconds*60;++i) {
        if(i%2 && i%paintStep) continue;
        const auto due=begin+std::chrono::duration_cast<Clock::duration>(i*period);
        const auto next=begin+std::chrono::duration_cast<Clock::duration>((i+paintStep)*period);
        std::this_thread::sleep_until(due);const auto started=Clock::now();
        const auto& frame=input.frames[static_cast<size_t>(60+i/2)];
        if(i%2==0){
            if(started>=begin+std::chrono::duration_cast<Clock::duration>((i+2)*period)) ++missedInput;
            view.queueSnapshot(frame,1);
        }
        if(i%paintStep) continue;
        Slot s;s.index=i/paintStep;s.nominalMs=ms(due-begin);s.startMs=ms(started-begin);s.lateMs=ms(started-due);
        s.endpoint=frame.presentation_end_samples;
        if(started>=next){s.dropped=true;s.finishMs=s.startMs;slots.push_back(s);continue;}
        const auto queued=Clock::now();
        view.presentationTickAt(juce::Time::getMillisecondCounterHiRes());const auto ticked=Clock::now();
        paint(view,image);const auto finished=Clock::now();s.finishMs=ms(finished-begin);
        s.queueMs=ms(queued-started);s.tickMs=ms(ticked-queued);s.paintMs=ms(finished-ticked);
        s.workMs=ms(finished-started);s.overrun=finished>next;slots.push_back(s);
    }
    std::this_thread::sleep_until(begin+std::chrono::seconds(durationSeconds));const auto end=Clock::now();
    const double cpuTime=cpu.seconds()-cpuBegin,wall=ms(end-begin)/1000.0;
    int drops=0,overruns=0;std::vector<double> work,paints;juce::Array<juce::var> records;
    for(const auto& s:slots){records.add(slotJson(s));if(s.dropped){++drops;continue;}overruns+=s.overrun;work.push_back(s.workMs);paints.push_back(s.paintMs);}
    auto result=object();set(result,"schema","hypha-freq-decision-options-v1");set(result,"source_id_caller_supplied",source);
    set(result,"width",width);set(result,"height",width*2/3);set(result,"fps",rate);set(result,"run",repetition);
    set(result,"measurement_seconds",durationSeconds);set(result,"warmup_seconds",2);
    set(result,"input_frames_measured",durationSeconds*30);set(result,"fixture_prefix_frames",60+durationSeconds*30);
    set(result,"wall_seconds",wall);set(result,"thread_cpu_seconds",cpuTime);set(result,"cpu_ms_per_second",1000*cpuTime/wall);
    set(result,"rendered",static_cast<int>(work.size()));set(result,"nominal_frames",durationSeconds*rate);set(result,"dropped",drops);set(result,"overruns",overruns);
    set(result,"valid",drops==0&&overruns==0&&missedInput==0&&static_cast<int>(work.size())==durationSeconds*rate&&wall>=durationSeconds&&cpuTime>=0);
    set(result,"missed_input_deadlines",missedInput);set(result,"input_hz",30);set(result,"fixture_sha256",input.sha256);
    set(result,"work_ms",distribution(work));set(result,"paint_ms",distribution(paints));set(result,"slots",records);
    set(result,"fixture","identical fixed 960-frame dense paired stereo trace at 30 Hz regardless of presentation rate");
    set(result,"smoothing","existing elapsed-ms response; no coefficient or production frame driver changes");
    set(result,"scope","full-component native offscreen CPU; all 30 Hz input updates retained; not DAW CPU or final acceptance");
    return result;
}
inline void film(int width,int rate,const juce::File& root,const juce::String& source) {
    const auto directory=root.getChildFile("frames-"+juce::String(width));check(directory.createDirectory().wasOk(),"cannot create film directory");
    SpectrumComponent view;prepare(view,width);juce::Image image(juce::Image::ARGB,width,width*2/3,true);
    const auto input=makeInput();const int paintStep=60/rate;
    view.queueSnapshot(input.frames.front(),1);const auto epoch=view.curveUpdateTimeForTest();
    juce::Array<juce::var> records;
    for(int event=0;event<720;++event) {
        const double seconds=double(event)/60;const auto& frame=input.frames[static_cast<size_t>(event/2)];
        if(event>0 && event%2==0) view.queueSnapshot(frame,1);
        if(event%paintStep) continue;
        if(event>0) view.presentationTickAt(epoch+seconds*1000);
        paint(view,image);
        if(event>=120){
            const int index=(event-120)/paintStep;const auto file=directory.getChildFile(juce::String::formatted("frame-%04d.png",index));
            freq_pixels::savePng(file,image);auto item=object();set(item,"index",index);set(item,"seconds",seconds-2);
            set(item,"endpoint",static_cast<juce::int64>(view.presentedEndpointForTest()));
            set(item,"arrays_sha256",freq_pixels::arraysFor(view,frame).arraysHash);records.add(item);
        }
    }
    auto meta=object();set(meta,"source",source);set(meta,"fps",rate);set(meta,"duration_seconds",10);set(meta,"width",width);
    set(meta,"frames",records);set(meta,"claim","deterministic virtual-clock film, no CPU claim; rendering is held until each next frame");
    saveJson(directory.getChildFile("manifest.json"),meta);
}
inline int main(const juce::File& directory,const juce::String& source,int rate,bool makeFilm) {
    check(!phaseProfileBuild,"options CPU/image requires uninstrumented binary");
    check(rate==30||rate==20||rate==15,"decision frame rate must be 30, 20 or 15");
    check(source.isNotEmpty()&&!directory.exists(),"source ID and new output directory required");
    check(directory.createDirectory().wasOk(),"cannot create decision directory");
    const i18n::ScopedLanguage english(i18n::Language::english);material_cache::Lifetime material;
    if(makeFilm){for(int width:{600,900}) film(width,rate,directory,source);return 0;}
    juce::Array<juce::var> summaries;bool all=true;
    for(int repetition=1;repetition<=3;++repetition)for(int width:{600,900}) {
        auto result=cpuRun(width,rate,repetition,source);all=all&&static_cast<bool>(result.getProperty("valid",false));
        saveJson(directory.getChildFile(juce::String(width)+"-run-"+juce::String(repetition)+".json"),result);
        std::cout<<"FREQ_OPTION "<<rate<<" "<<width<<" "<<repetition<<" "<<result.getProperty("cpu_ms_per_second",0).toString()<<" valid="<<all<<std::endl;
        summaries.add(result);
    }
    auto summary=object();set(summary,"valid",all);set(summary,"results",summaries);saveJson(directory.getChildFile("summary.json"),summary);return all?0:2;
}
}
