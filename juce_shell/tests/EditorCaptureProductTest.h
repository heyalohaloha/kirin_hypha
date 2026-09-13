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
        started=juce::Time::getMillisecondCounterHiRes(); startTimer(10);
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
    int phase=0,blocks=0,dirty=0;double started=0;
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
            // Allow the production identity/IO startup timer to complete, without an OS library.
            if(juce::Time::getMillisecondCounterHiRes()-started<700) return;
            access=processor->referenceAuditionSnapshot().captureAccess;require(bool(access),"shipping POST exposes Capture without B");
            require(access->request(hypha::reference_audition::ACaptureAccess::start),"shipping Capture start");++phase;return;
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
        const auto restored=snapshot.captureAccess->snapshot();if(!restored.held)return;
        require(restored.held->restored && restored.held->frames==192000 && !snapshot.captureAccess->active && !snapshot.bSelected,"shipping state restoration remains historical and silent");
        close();stopTimer();std::cout<<"PASS Capture processor: free/unconnected POST, hidden editor, unchanged A, host dirty state, save/reopen"<<std::endl;done();
    }
};
