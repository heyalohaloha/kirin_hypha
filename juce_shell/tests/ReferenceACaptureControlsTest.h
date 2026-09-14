#pragma once
#include "../src/HyphaReferenceComponent.h"
#include "../src/HyphaReferenceComparisonView.h"
#include <iostream>
namespace hypha::tests
{
inline void verifyCaptureControls()
{
    const auto check=[](bool v,const char* why){if(!v){std::cerr<<"Capture UI: "<<why<<'\n';std::exit(1);}};
    auto access=std::make_shared<reference_audition::ACaptureAccess>();
    auto captured=std::make_shared<reference_audition::ACaptureData>(); captured->rate=48000; captured->channels=2;
    captured->id="held-a"; captured->created=juce::Time::currentTimeMillis(); captured->frames=48000*120; captured->complete=true;
    auto timeline=std::make_shared<reference_audition::VisualTimeline>(); timeline->capture=captured;
    timeline->binding.key="held-a"; timeline->binding.hostRate=48000; timeline->binding.channels=2; timeline->pass=timeline->revision=1;
    for(int i=0;i<1200;++i) {
        reference_audition::ACaptureBin a; a.offset=std::uint64_t(i)*4800; a.value.frames=4800;
        const auto value=0.5+0.3*std::sin(i*0.021);
        a.value.peak[0]=a.value.peak[1]=value; a.value.rms[0]=a.value.rms[1]=value*0.5;
        a.value.short_lufs=i>=29 ? -14+std::sin(i*0.015) : NAN; a.value.crest_db=6;
        captured->bins.push_back(a); reference_audition::VisualPairBin pair; pair.a=a.value; pair.pass=1; timeline->bins.push_back(pair);
    }
    reference_audition::ACaptureState held; held.held=held.shown=captured; held.phase=reference_audition::ACapturePhase::held;
    access->publish(held); access->capturedView=true;
    reference_ui::State state; state.captureAccess=access; state.visualTimeline=timeline; state.separateComparisons=true;
    state.comparisonSlot=1; state.title="VERSION"; state.blindPhase=reference_ui::BlindPhase::unavailable;
    reference_ui::Component component; int audio=0; component.onSelectA=component.onSelectB=[&]{++audio;};
    for(int width:{300,375,450,600,900}) {
        component.setPresentationContext(presentation::forEditor(width,width*2/3));
        component.setSize(width-12,width==900 ? 470 : width*2/3-64); component.setState(state);
        auto* controls=component.findChildWithID("capture-a-controls"); auto* graph=component.findChildWithID("reference-comparison-view");
        check(controls && controls->isVisible() && component.getLocalBounds().contains(controls->getBounds()),"controls fit every size");
        check(graph->isVisible() && graph->getHeight()>0 && !graph->getBounds().intersects(controls->getBounds()),"held A is visible without B and never overlaps controls");
        auto* view=dynamic_cast<juce::Button*>(controls->findChildWithID("capture-a-view")); check(view && view->isVisible(),"LIVE/CAPTURED available");
        view->onClick(); check(!access->capturedView && audio==0,"view choice never switches audio"); access->capturedView=true; component.setState(state);
        const auto output=juce::SystemStats::getEnvironmentVariable("KIRIN_REFERENCE_VISUAL_OUTPUT",{});
        if(output.isNotEmpty()) {
            juce::File root(output); check(root.createDirectory(),"capture image directory");
            juce::Image image(juce::Image::ARGB,component.getWidth(),component.getHeight(),true); juce::Graphics g(image); component.paintEntireComponent(g,true);
            juce::FileOutputStream stream(root.getChildFile("capture-"+juce::String(width)+".png")); check(juce::PNGImageFormat().writeImageToStream(image,stream),"capture rendered");
        }
        state.blindPhase=reference_ui::BlindPhase::active; component.setState(state);
        check(!controls->isVisible() && controls->getTitle().isEmpty() && !graph->isVisible(),"Blind hides capture data and accessibility");
        state.blindPhase=reference_ui::BlindPhase::unavailable;
    }
    {
        reference_ui::ComparisonView graph; graph.setSize(600,300);
        auto small=std::make_shared<reference_audition::VisualTimeline>(*timeline);
        auto first=std::make_shared<reference_audition::ACaptureData>(*captured); first->frames=4800; first->bins.resize(1);
        small->capture=first; small->bins.resize(1); graph.update(small,-1,presentation::forEditor(900,600),false);
        check(std::abs(graph.selectedRange().getEnd()-0.1)<1e-9,"first capture bin fits visible range");
        graph.update(timeline,-1,presentation::forEditor(900,600),false);
        check(std::abs(graph.selectedRange().getEnd()-120)<1e-9,"same capture grows to its full duration");
        graph.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)); const auto manual=graph.selectedRange();
        auto longer=std::make_shared<reference_audition::VisualTimeline>(*timeline);
        auto next=std::make_shared<reference_audition::ACaptureData>(*captured); next->frames=48000*180; longer->capture=next;
        graph.update(longer,-1,presentation::forEditor(900,600),false);
        check(graph.selectedRange()==manual,"manual range survives continued capture growth");
    }
    access->active=true; held.phase=reference_audition::ACapturePhase::capturing; access->publish(held); component.setState(state);
    auto* controls=component.findChildWithID("capture-a-controls"); auto* finish=dynamic_cast<juce::Button*>(controls->findChildWithID("capture-a-action"));
    finish->onClick(); check(access->pending==reference_audition::ACaptureAccess::finish && audio==0,"finish targets capture only");
    std::cout<<"Capture A UI: all five sizes, A without B, LIVE/CAPTURED, Blind concealment, FINISH PASS\n";
}
}
