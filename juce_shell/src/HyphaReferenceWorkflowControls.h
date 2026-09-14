#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "HyphaTheme.h"
#include "reference_audition/ReferenceWorkflowModel.h"

namespace hypha::reference_ui
{
class WorkflowControls final : public juce::Component
{
public:
    WorkflowControls()
    {
        configure(start,"TODAY","Start or continue the latest review.");
        configure(bookmark,"BOOKMARK","Prepare the latest saved comparison. Audio stays on A.");
        configure(back,"<","Prepare the previous review item. Audio stays on A.");
        configure(confirmed,"CHECK + NEXT","Mark this item checked and prepare the next item.");
        configure(deferred,"HOLD + NEXT","Hold this item and prepare the next item.");
        configure(end,"END","End this workflow and restore the normal Reference selection.");
        for (auto* button : {&start,&bookmark,&back,&confirmed,&deferred,&end}) addChildComponent(*button);
        start.onClick=[this]{if(onStart)onStart();}; bookmark.onClick=[this]{if(onBookmark)onBookmark();};
        back.onClick=[this]{if(onBack)onBack();}; confirmed.onClick=[this]{if(onConfirmed)onConfirmed();};
        deferred.onClick=[this]{if(onDeferred)onDeferred();}; end.onClick=[this]{if(onEnd)onEnd();};
    }
    std::function<void()> onStart,onBookmark,onBack,onConfirmed,onDeferred,onEnd;
    void update(const reference_audition::WorkflowView& next,bool blind,bool compact)
    {
        state=next; compactLayout=compact;
        const bool active=!blind&&state.mode!=reference_audition::WorkflowView::Mode::normal
            &&state.status!=reference_audition::WorkflowView::Status::resumeAvailable;
        const bool idle=!blind&&!active;
        start.setVisible(idle&&(state.reviewAvailable||state.status==reference_audition::WorkflowView::Status::resumeAvailable));
        bookmark.setVisible(idle&&state.bookmarkAvailable);
        back.setVisible(active); confirmed.setVisible(active); deferred.setVisible(active); end.setVisible(active);
        const bool saving=state.status==reference_audition::WorkflowView::Status::saving;
        back.setEnabled(state.canMoveBack&&!saving);
        confirmed.setEnabled(state.status==reference_audition::WorkflowView::Status::ready);
        deferred.setEnabled(state.canAdvance&&!saving); end.setEnabled(state.canEnd&&!saving);
        bookmark.setButtonText(compact?"MARK":"BOOKMARK"); resized(); repaint();
    }
    int preferredHeight() const noexcept
    { return state.mode==reference_audition::WorkflowView::Mode::normal?(compactLayout?20:24):(compactLayout?42:52); }
    void resized() override
    {
        auto area=getLocalBounds(); const int gap=compactLayout?2:4;
        if(state.mode==reference_audition::WorkflowView::Mode::normal)
        {
            if(bookmark.isVisible()) bookmark.setBounds(area.removeFromRight(compactLayout?48:82));
            if(bookmark.isVisible()) area.removeFromRight(gap);
            if(start.isVisible()) start.setBounds(area.removeFromRight(compactLayout?52:76));
            return;
        }
        area.removeFromTop(compactLayout?18:24);
        const int endWidth=compactLayout?35:48,backWidth=compactLayout?28:36;
        end.setBounds(area.removeFromRight(endWidth)); area.removeFromRight(gap);
        deferred.setBounds(area.removeFromRight((area.getWidth()-backWidth-gap*2)/2)); area.removeFromRight(gap);
        confirmed.setBounds(area.removeFromRight(area.getWidth()-backWidth-gap)); area.removeFromRight(gap);
        back.setBounds(area);
    }
    void paint(juce::Graphics& g) override
    {
        if(state.mode==reference_audition::WorkflowView::Mode::normal)return;
        auto title=juce::String(state.itemIndex+1)+"/"+juce::String(state.itemCount)+"  "+state.itemTitle;
        if(state.status==reference_audition::WorkflowView::Status::preparing)title+="  /  PREPARING";
        else if(state.status==reference_audition::WorkflowView::Status::saving)title+="  /  SAVING";
        else if(state.status==reference_audition::WorkflowView::Status::rejected)title+="  /  CHANGED";
        g.setColour(state.status==reference_audition::WorkflowView::Status::rejected?COL_LED_YELLOW:COL_TEXT_SECONDARY);
        g.setFont(juce::Font(compactLayout?10.0f:12.0f));
        g.drawFittedText(title,getLocalBounds().removeFromTop(compactLayout?18:24),juce::Justification::centredLeft,1,1.0f);
    }
private:
    static void configure(juce::TextButton& button,const juce::String& label,const juce::String& tooltip)
    {
        button.setButtonText(label); button.setTooltip(tooltip);
        button.setColour(juce::TextButton::buttonColourId,kFieldFill);
        button.setColour(juce::TextButton::textColourOffId,COL_TEXT_SECONDARY);
    }
    reference_audition::WorkflowView state; bool compactLayout=false;
    juce::TextButton start,bookmark,back,confirmed,deferred,end;
};
}
