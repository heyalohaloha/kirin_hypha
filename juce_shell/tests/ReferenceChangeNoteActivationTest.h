#pragma once

#include "../src/HyphaReferenceChangeNoteRows.h"
#include <stdexcept>
#include <vector>
#include "ReferenceSelectionReopenTest.h" // last: platform run-loop declaration

namespace hypha::tests
{
inline void verifyReferenceChangeNoteActivation()
{
    const auto require=[](bool ok,const char* why){ if(!ok) throw std::runtime_error(why); };
    using Action=juce::AccessibilityActionType;
    juce::Component window;
    reference_ui::ChangeNoteRows rows;
    window.setBounds(50,50,260,76); rows.setBounds(window.getLocalBounds());
    window.addAndMakeVisible(rows);
    std::vector<reference_ui::ChangeNoteRows::Item> items;
    for(int i=0;i<30;++i)
        items.push_back({"note-"+juce::String(i),"Excerpt "+juce::String(i),
            monoFont(presentation::forEditor(300,200),typography::TextRole::action),COL_NORMAL,25});
    rows.setItems(std::move(items),240);
    window.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    window.setVisible(true);
    require(afterQueuedMessages([]{}),"notes fixture peer is shown");
    auto* first=dynamic_cast<juce::Button*>(rows.getViewedComponent()->getChildComponent(0));
    require(first && first->isShowing() && first->getTitle()=="Excerpt 0","first pooled row really bound");
    std::vector<juce::String> opened;
    rows.onSelect=[&](const juce::String& id){ opened.push_back(id); };
    first->grabKeyboardFocus();
    require(afterQueuedMessages([]{}) && first->hasKeyboardFocus(false),"real pooled row has keyboard focus");
    auto& surface=static_cast<juce::Component&>(*first);
    require(surface.keyPressed({juce::KeyPress::returnKey,juce::ModifierKeys(),0}) &&
            surface.keyPressed({juce::KeyPress::returnKey,juce::ModifierKeys::shiftModifier,0}) &&
            opened==std::vector<juce::String>{"note-0","note-0"},"Return and Shift+Return select the original ID immediately");
    const auto event=[&](int mods)
    {
        const juce::Point<float> point(6.0f,6.0f); const auto now=juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(mods),
            0,0,0,0,0,first,first,now,point,now,1,false);
    };
    surface.mouseDown(event(juce::ModifierKeys::leftButtonModifier));
    require(first->isDown(),"actual pooled mouse press is down");
    surface.mouseUp(event(0));
    require(opened.size()==3 && opened.back()=="note-0","a same-binding mouse click selects the correct ID");
    auto* handler=first->getAccessibilityHandler();
    require(handler!=nullptr,"pooled row exposes an accessibility handler");
    const auto oldActions=handler->getActions();
    require(oldActions.invoke(Action::press) && afterQueuedMessages([]{}) && opened.size()==4
            && opened.back()=="note-0","real accessibility action selects the still-bound ID");

    surface.mouseDown(event(juce::ModifierKeys::leftButtonModifier));
    require(first->isDown() && oldActions.invoke(Action::press),"mouse press and queued accessibility press start on row zero");
    rows.setViewPosition(0,250);
    require(first==rows.getViewedComponent()->getChildComponent(0) && first->getTitle()=="Excerpt 10",
            "scroll actually rebinds the same pooled component to a different note");
    surface.mouseUp(event(0));
    require(oldActions.invoke(Action::press) && afterQueuedMessages([]{}) && opened.size()==4,
            "rebound row rejects both an in-flight mouse release and old accessibility actions");
    auto* reboundHandler=first->getAccessibilityHandler();
    require(reboundHandler!=nullptr && reboundHandler->getActions().invoke(Action::press)
            && afterQueuedMessages([]{}) && opened.size()==5 && opened.back()=="note-10",
            "the new accessibility action selects its own replacement ID");
}
}
