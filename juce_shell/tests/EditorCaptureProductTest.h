#pragma once
#include "../src/PluginProcessor.h"
#include <functional>
#include <iostream>
class CaptureProductContract final : private juce::Timer, private juce::AudioProcessorListener
{
public:
    explicit CaptureProductContract(std::function<void()> next):done(std::move(next))
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_VST3);
        processor=std::make_unique<Processor>(Processor::Role::Post); prepareHost(); processor->addListener(this);
        // This contract declares a realtime host. Feed each 4,800-frame callback at its actual
        // 100 ms cadence so the test does not accidentally turn into an unlabelled offline-render
        // queue stress test.
        started=juce::Time::getMillisecondCounterHiRes(); startTimer(100);
    }
    ~CaptureProductContract() override { stopTimer(); close(); }
private:
    using Processor=KirinHyphaProcessorBase;
    struct Clock:juce::AudioPlayHead {
        juce::Optional<PositionInfo> getPosition() const override {PositionInfo p;p.setIsPlaying(playing);p.setTimeInSamples(position);return p;}
        bool playing=false;juce::int64 position=0;
    } clock;
    std::unique_ptr<Processor> processor;
    std::shared_ptr<hypha::reference_audition::ACaptureAccess> access;
    juce::AudioBuffer<float> buffer{2,4800};juce::MidiBuffer midi;juce::MemoryBlock saved;
    std::function<void()> done;
    int phase=0,blocks=0,dirty=0,restoredTicks=0;double started=0;
    void require(bool value,const char* why) {if(!value){std::cerr<<"Capture processor: "<<why<<std::endl;std::exit(1);}}
    void prepareHost()
    {
        auto layout=processor->getBusesLayout();layout.inputBuses.set(0,juce::AudioChannelSet::stereo());layout.outputBuses.set(0,juce::AudioChannelSet::stereo());
        require(processor->setBusesLayout(layout),"host negotiates stereo input and output");
        processor->setRateAndBufferSizeDetails(48000,4800);processor->setPlayHead(&clock);processor->setNonRealtime(false);processor->prepareToPlay(48000,4800);
    }
    void close() {if(processor){processor->removeListener(this);processor->releaseResources();processor.reset();}}
    void audioProcessorParameterChanged(juce::AudioProcessor*,int,float) override {}
    void audioProcessorChanged(juce::AudioProcessor*,const ChangeDetails& details) override {if(details.nonParameterStateChanged) ++dirty;}
    void timerCallback() override
    {
        if(juce::Time::getMillisecondCounterHiRes()-started>=12000)
        {
            const auto state=access ? access->snapshot() : hypha::reference_audition::ACaptureState{};
            std::cerr<<"Capture lifecycle phase="<<phase<<" blocks="<<blocks<<" dirty="<<dirty
                <<" active="<<bool(access && access->active)<<" capturePhase="<<int(state.phase)
                <<" heldFrames="<<(state.held ? state.held->frames : 0)<<" message="<<state.message<<std::endl;
            require(false,"host Capture lifecycle timeout");
        }
        if(phase==0)
        {
            // Production identity/IO startup is asynchronous and can take longer on a loaded CI
            // runner. Poll the product fact until the contract's existing bounded timeout.
            access=processor->referenceAuditionSnapshot().captureAccess;if(!access)return;
            // 2026-10-04：製品は A を取り込まない。古い版が取り込んで DAW の曲に保存した状態を、残っている取り込みの
            // 仕組みに直接命じて作る（製品の画面・処理からは命じない）。
            require(access->request(hypha::reference_audition::ACaptureAccess::start),"an older version's Capture starts");++phase;return;
        }
        if(phase==1) {if(!access->active)return;clock.playing=true;++phase;}
        if(phase==2)
        {
            for(int i=0;i<4800;++i) {const auto v=float(0.1*std::sin(juce::MathConstants<double>::twoPi*1000*double(clock.position+i)/48000));buffer.setSample(0,i,v);buffer.setSample(1,i,-v);}
            juce::AudioBuffer<float> original(buffer);processor->processBlock(buffer,midi);
            for(int c=0;c<2;++c) require(std::memcmp(buffer.getReadPointer(c),original.getReadPointer(c),4800*sizeof(float))==0,"shipping A stays bit identical during Capture");
            require(processor->getLatencySamples()==0,"Capture adds zero reported samples");
            clock.position+=4800;if(++blocks<40)return;
            clock.playing=false;processor->processBlock(buffer,midi);++phase;return;
        }
        if(phase==3)
        {
            const auto state=access->snapshot();if(access->active || !state.held || dirty==0)return;
            require(state.held->complete && state.held->frames==192000,"shipping callback range completes without editor or B");
            processor->getStateInformation(saved);require(saved.getSize()>1000,"host state contains captured summary");close();
            processor=std::make_unique<Processor>(Processor::Role::Post);processor->setStateInformation(saved.getData(),int(saved.getSize()));
            prepareHost();++phase;return;
        }
        const auto snapshot=processor->referenceAuditionSnapshot();if(!snapshot.captureAccess)return;
        // 古い版が保存した取り込みは読み込まない（取り込んだ A の表示に替えない・照合に解析を使わない）。復元が
        // 走るなら走る時間（1 秒）を待ってから確かめる。
        if(++restoredTicks<10)return;
        const auto restored=snapshot.captureAccess->snapshot();
        require(!restored.held && !snapshot.captureAccess->capturedView && !snapshot.captureAccess->active && !snapshot.bSelected,
                "an older version's saved Capture is skipped on reopen");
        close();stopTimer();std::cout<<"PASS Capture processor: unchanged A while an older Capture runs, its saved Capture is skipped on reopen"<<std::endl;done();
    }
};
