#pragma once
#include "../src/HyphaReferenceComponent.h"
#include "../src/HyphaObservatoryContract.h"
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
        const auto body=observatory::shellLayout(observatory::Role::post,observatory::presetForWidth(width),observatory::GuidePresence::absent).body;
        component.setSize(body.width,body.height); component.setState(state);
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
    // A failed retry must remain secondary to newly observed input differences.
    auto failed=held; failed.outcome={reference_audition::CaptureOutcome::interrupted,7,captured->id};
    failed.message="Interrupted / previous capture kept";
    failed.unitStatus={2}; failed.unitPass={9}; failed.observationPass=9; failed.observationFresh=true;
    failed.confirmedTimingEpoch=3; access->currentTimingEpoch=3; access->inputActive=true; access->publish(failed);
    reference_ui::CaptureControls status; status.setSize(600,40); status.update(access,false,presentation::forEditor(900,600));
    check(status.getTitle()=="A DIFFERS" && status.getDescription().contains("interrupted"),"current input fact and retry failure coexist");
    const auto render=[&] { juce::Image image(juce::Image::ARGB,600,40,true); juce::Graphics g(image); status.paintEntireComponent(g,true); return image; };
    const auto changed=render(); failed.unitStatus={1}; access->publish(failed); status.update(access,false,presentation::forEditor(900,600)); const auto matching=render();
    int pixels=0; for(int y=0;y<20;++y) for(int x=0;x<300;++x) if(changed.getPixelAt(x,y)!=matching.getPixelAt(x,y)) ++pixels;
    check(pixels>20,"old failure cannot conceal the changed primary in actual rendering");
    auto partial=std::make_shared<reference_audition::ACaptureData>(*captured); partial->complete=false;
    failed.held=failed.shown=partial; failed.unitStatus={2}; failed.observationFresh=false;
    const auto partialStatus=reference_ui::presentCapture(failed,3);
    check(partialStatus.primary=="LAST: A DIFFERS" && partialStatus.secondary.contains("Partial capture"),"partial coverage and stale evidence remain distinct");
    for(int width:{300,375,450,600,900})
    {
        access->publish(failed); component.setPresentationContext(presentation::forEditor(width,width*2/3));
        const auto body=observatory::shellLayout(observatory::Role::post,observatory::presetForWidth(width),observatory::GuidePresence::absent).body;
        component.setSize(body.width,body.height); component.setState(state);
        auto* row=component.findChildWithID("capture-a-controls"); auto* graph=component.findChildWithID("reference-comparison-view");
        check(row && row->getTitle()=="LAST: A DIFFERS" && row->getHeight()>=28,"stale difference, partial capture and failure have two visible lines at every size");
        check(graph->getHeight()>0 && !row->getBounds().intersects(graph->getBounds()),"failure row keeps a distinct graph region");
        const auto output=juce::SystemStats::getEnvironmentVariable("KIRIN_REFERENCE_VISUAL_OUTPUT",{});
        if(output.isNotEmpty()) { juce::Image image(juce::Image::ARGB,component.getWidth(),component.getHeight(),true); juce::Graphics g(image); component.paintEntireComponent(g,true); juce::FileOutputStream out(juce::File(output).getChildFile("capture-failure-"+juce::String(width)+".png")); check(juce::PNGImageFormat().writeImageToStream(image,out),"failure render saved"); }
    }
    {
        auto checkState=state; checkState.comparisonSlot=2; checkState.presets={{"p1","Factory preset"},{"p2","Another preset"}};
        checkState.presetId="p1"; checkState.checks={{"c1","Dynamics"},{"c2","Low end"}}; checkState.checkId="c1"; checkState.actionText="PREPARE";
        const auto body=observatory::shellLayout(observatory::Role::post,observatory::sizePresets[0],observatory::GuidePresence::absent).body;
        component.setPresentationContext(presentation::forEditor(300,200)); component.setSize(body.width,body.height); component.setState(checkState);
        auto* row=component.findChildWithID("capture-a-controls");
        for(const char* id:{"reference-version","reference-preset","reference-check"}) {
            auto* selector=component.findChildWithID(id); check(selector && selector->getHeight()>=18 && component.getLocalBounds().contains(selector->getBounds()),"C selectors keep usable bounds in the actual 100 percent body");
            check(!row->getBounds().intersects(selector->getBounds()),"capture failure never overlaps C selection");
        }
        check(row->getHeight()>=28 && component.getLocalBounds().contains(row->getBounds()),"C keeps the full failure status at actual host bounds");
        component.setState(state);
    }
    {
        auto keyboardAccess=std::make_shared<reference_audition::ACaptureAccess>(); reference_ui::CaptureControls keys;
        keys.setSize(600,24); keys.update(keyboardAccess,false,presentation::forEditor(900,600));
        auto* button=keys.findChildWithID("capture-a-action");
        button->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
        keys.update(keyboardAccess,false,presentation::forEditor(900,600));
        button->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
        check(!keyboardAccess->operationView().cancellation,"held Return cannot become Cancel after Start updates the UI");
        button->keyStateChanged(false); button->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
        check(keyboardAccess->operationView().cancellation,"a released and pressed Return explicitly cancels");
    }
    {
        auto mouseAccess=std::make_shared<reference_audition::ACaptureAccess>(); reference_ui::CaptureControls mouse;
        mouse.setSize(600,24); mouse.update(mouseAccess,false,presentation::forEditor(900,600));
        auto* component=mouse.findChildWithID("capture-a-action"); auto* button=dynamic_cast<juce::Button*>(component);
        const auto event=[&](int count) { const auto now=juce::Time::getCurrentTime(); return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{5,5},{},0,0,0,0,0,component,component,now,{5,5},now,count,false); };
        component->mouseDown(event(1)); component->mouseUp(event(1));
        check(mouseAccess->operationView().busy(),"first complete mouse gesture starts Capture");
        mouse.update(mouseAccess,false,presentation::forEditor(900,600));
        button->setState(juce::Button::buttonDown); // Exercise an outstanding pressed/flash state too.
        component->mouseDown(event(2)); component->mouseUp(event(2));
        check(!mouseAccess->operationView().cancellation,"second mouse-up cannot cancel through a lingering pressed state");
        component->mouseDown(event(1)); component->mouseUp(event(1));
        check(mouseAccess->operationView().cancellation,"a new independent click explicitly cancels");
        mouseAccess->complete(mouseAccess->operationView().id); mouse.update(mouseAccess,false,presentation::forEditor(900,600));
        component->mouseDown(event(1));
        check(mouseAccess->request(reference_audition::ACaptureAccess::start),"another entry accepts Start while a gesture is held");
        mouse.update(mouseAccess,false,presentation::forEditor(900,600)); component->mouseUp(event(1));
        check(!mouseAccess->operationView().cancellation,"held Start gesture is never reinterpreted as the newly displayed Cancel");
    }
    check(access->request(reference_audition::ACaptureAccess::start),"fixture capture reserves start");
    access->advance(access->operationView().id,reference_audition::CaptureOperationPhase::capturing);
    access->active=true; held.phase=reference_audition::ACapturePhase::capturing; access->publish(held); component.setState(state);
    auto* controls=component.findChildWithID("capture-a-controls"); auto* finish=dynamic_cast<juce::Button*>(controls->findChildWithID("capture-a-action"));
    finish->onClick(); check(access->pending==reference_audition::ACaptureAccess::finish && audio==0,"finish targets capture only");
    std::cout<<"Capture A UI: all five sizes, A without B, LIVE/CAPTURED, Blind concealment, FINISH PASS\n";
}
}
