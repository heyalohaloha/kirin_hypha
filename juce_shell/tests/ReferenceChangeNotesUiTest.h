#pragma once
#include "../src/HyphaReferenceChangeNotes.h"
#include <stdexcept>
#include <iostream>

namespace hypha::tests
{
inline void verifyReferenceChangeNotesUi(const juce::String& preview = {})
{
    const auto require=[](bool ok,const char* why){if(!ok) throw std::runtime_error(why);};
    using reference_audition::ChangeNotes;
    auto notes=std::make_shared<ChangeNotes>();
    notes->artifactSha256="synthetic-notes"; notes->sampleRateHz=48000;
    notes->totalSampleFrames=48000*120; notes->openCount=2;
    notes->notes={{"first#1","first","Raise the synthetic voice.\n<plain text>",1,48000*12,12,false},
                  {"first#2","first","Review the synthetic ending.",2,48000*83,83,false},
                  {"second#1","second","Completed synthetic change.",1,48000*90,90,true}};
    int images=0;
    for (auto language:{i18n::Language::english,i18n::Language::japanese})
        for (int width:{300,600,900}) {
            const i18n::ScopedLanguage scoped(language);
            reference_ui::ChangeNotesView view; view.setSize(width,width*2/3);
            const auto context=presentation::forEditor(width,width*2/3);
            view.setNotes(notes,false,context,"version-one");
            require(view.isVisible() && view.noteCount()==3 && view.displayedBody().isEmpty(),"notes start without open body");
            require(view.getNumChildComponents()==3,"notes use constant control count");
            const auto timeText=i18n::tr("Source time");
            const auto timeFont=displayTextFont(timeText,context,typography::TextRole::listenChoice,typography::Composition::information);
            require(timeFont.getStringWidthFloat(timeText)<=view.sourceLabelBounds().getWidth()
                && view.timelineBounds().getX()>view.sourceLabelBounds().getRight(),"source-time label fits in both languages without truncation or axis overlap");
            require(reference_ui::ChangeNotesView::sourceTime(83)=="1:23","source seconds not DAW time");
            const auto save=[&](const juce::String& stage) {
                if(preview.isEmpty()) return;
                const juce::File directory(preview); require(directory.isDirectory(),"notes preview directory exists");
                const auto path=directory.getChildFile("notes-"+juce::String(language==i18n::Language::japanese?"ja-":"en-")
                    +juce::String(width)+"-"+stage+".png");
                require(!path.exists(),"notes image must be fresh");auto out=path.createOutputStream();
                require(out && juce::PNGImageFormat().writeImageToStream(view.createComponentSnapshot(view.getLocalBounds()),*out),"notes PNG");
                ++images;
            };
            const auto checkToggleText=[&] {
                auto* toggle=dynamic_cast<HyphaTextButton*>(view.findChildWithID("reference-change-notes-toggle"));
                require(toggle && toggle->isVisible(),"notes action toggle is visible");
                // Exact paintButton path: mono action/shell, shown/native text,
                // local bounds reduced(6,2), one line with no horizontal shrinking.
                const auto font=view.toggleFont();
                const auto text=toggle->displayedText();
                const auto inner=toggle->getLocalBounds().reduced(6,2);
                const auto needed=text_style::shownWidth(font,text,text_style::LabelPolicy::fixed);
                std::cout<<"NOTES_TOGGLE_TEXT width="<<width<<" language="
                    <<(language==i18n::Language::japanese?"ja":"en")<<" text="<<text
                    <<" font="<<font.getHeight()<<" textWidth="<<needed<<" inner="<<inner.toString()<<std::endl;
                require(text.isNotEmpty() && needed<=static_cast<float>(inner.getWidth()),
                    "notes action displayed text fits its actual paint width without truncation");
                require(font.getHeight()>=12.0f && font.getHeight()<=inner.getHeight(),
                    "notes action uses at least 12px and fits actual paint height");
                require(view.headingBounds().getRight()<=toggle->getX()-6,
                    "notes header reserves the actual action width and separation");
                require(view.getLocalBounds().contains(toggle->getBounds()),"notes action remains inside component");
            };
            save("marks"); checkToggleText();
            require(view.selectNote(0) && view.displayedBody()==notes->notes[0].text,"selection reveals exact plain text only");
            auto* body=dynamic_cast<juce::TextEditor*>(view.findChildWithID("reference-change-notes-body"));
            require(body && body->isVisible() && body->isReadOnly() && body->getFont().getHeight()>=12,"body readable and read-only");
            require(view.getLocalBounds().contains(body->getBounds()),"body stays inside viewport");
            save("body"); checkToggleText();
            view.setNotes(notes,false,context,"version-two");
            require(view.displayedBody().isEmpty() && view.selectedNote()<0,"same receipt new selection clears body");
            view.selectNote(1);
            view.setNotes(notes,true,context,"version-two");
            require(!view.isVisible() && view.noteCount()==0 && view.displayedBody().isEmpty()
                && view.getTitle().isEmpty() && view.getDescription().isEmpty(),"blind clears notes and accessibility strings");
            for(auto* child:view.getChildren()) require(!child->isVisible() && child->getTitle().isEmpty()
                && child->getDescription().isEmpty(),"blind clears child visibility and accessibility");
            // A hidden component's own snapshot is not a host-composited BLIND screenshot.
            // The parent integration fixture must prove it is absent from the actual V surface.
            view.setNotes(notes,false,context,"version-two");
            require(view.displayedBody().isEmpty(),"return from blind does not reopen old text");
            view.setNotes({},false,context,"missing"); require(view.noteCount()==0,"missing notes clears prior data");
        }
    auto many=std::make_shared<ChangeNotes>(*notes); many->notes.clear(); many->openCount=1000;
    for(int i=0;i<1000;++i) many->notes.push_back({"synthetic#"+juce::String(i+1),"synthetic","Synthetic change",i+1,i*4800,i*.1,false});
    reference_ui::ChangeNotesView view;view.setSize(300,200);
    view.setNotes(many,false,presentation::forEditor(300,200),"many");
    auto* virtualList=dynamic_cast<reference_ui::ChangeNoteRows*>(view.findChildWithID("reference-change-notes-list"));
    auto* toggle=dynamic_cast<juce::Button*>(view.findChildWithID("reference-change-notes-toggle"));
    require(virtualList && toggle,"thousand-note virtual list controls");
    toggle->onClick();
    require(view.noteCount()==1000 && view.getNumChildComponents()==3 && virtualList->rowCount()==1000
        && virtualList->visibleButtonCount()>0 && virtualList->visibleButtonCount()<20,
        "thousand notes use a viewport-sized button pool");
    require(view.selectNote(999),"the full thousandth note remains selectable");
    require(!view.selectNote(1000),"out of range selection rejected");
    int expansion=0; view.onExpansionChanged=[&](bool){++expansion;};
    view.setNotes(notes,false,presentation::forEditor(300,200),"compact");
    expansion=0; // Count the compact open, not the preceding selection-change collapse.
    view.setSize(288,44);
    require(!view.isExpanded() && view.minimumHeight()==44,"compact strip stays collapsed");
    view.selectNote(0); view.setSize(288,90);
    auto* body=dynamic_cast<juce::TextEditor*>(view.findChildWithID("reference-change-notes-body"));
    require(view.isExpanded() && expansion==1 && body && body->getHeight()>=60
        && view.getLocalBounds().contains(body->getBounds()),"small expanded pane leaves usable plain-text scroll area");
    std::cout<<"REFERENCE_CHANGE_NOTES_UI_CASES=6 IMAGES="<<images<<'\n';
}
}
