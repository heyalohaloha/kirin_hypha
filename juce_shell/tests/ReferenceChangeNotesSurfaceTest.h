#pragma once
#include "ReferenceGuideContractTest.h"
#include "ReferenceControlLookup.h"
#include "../src/HyphaReferenceChangeNotes.h"
#include <stdexcept>
namespace hypha::tests
{
inline void verifyReferenceChangeNotesSurface(const juce::String& preview)
{
    const auto require=[](bool ok,const char* why){if(!ok) throw std::runtime_error(why);};
    int images=0;
    for(auto language:{i18n::Language::english,i18n::Language::japanese})
    for(int width:{300,600,900})
    {
        const i18n::ScopedLanguage scoped(language);
        observatory::View shell(observatory::Role::post);
        shell.setSize(width,width*2/3); shell.setDomain(observatory::Domain::reference);
        shell.setExternalAnalysisBodyActive(true);
        reference_ui::Component panel; shell.addAndMakeVisible(panel);
        panel.setPresentationContext(presentation::forEditor(width,width*2/3));
        panel.setBounds(shell.analysisBodyBounds());
        auto state=reference_guide_contract::named("ready");
        state.comparisonSlot=1; state.versionId="synthetic-version";
        auto notes=std::make_shared<reference_audition::ChangeNotes>();
        notes->artifactSha256="synthetic"; notes->sampleRateHz=48000; notes->totalSampleFrames=48000*120; notes->openCount=1;
        notes->notes={{"first#1","first",language==i18n::Language::japanese
            ? juce::String::fromUTF8(u8"この箇所の声を少し上げてください。\n<本文は文字のまま>") : juce::String("Raise the voice here.\n<plain text>"),
            1,48000*83,83,false}};
        state.changeNotes=notes;
        int audioCommands=0;
        panel.onSelectA=[&]{++audioCommands;}; panel.onSelectB=[&]{++audioCommands;};
        panel.onSelectC=[&]{++audioCommands;}; panel.onSelectRef=[&]{++audioCommands;};
        panel.onMatch=[&]{++audioCommands;};
        panel.setState(state);
        auto* view=dynamic_cast<reference_ui::ChangeNotesView*>(panel.findChildWithID("reference-change-notes"));
        auto* comparison=panel.findChildWithID("reference-comparison-view");
        require(view && view->isVisible() && view->getHeight()>=44,"V notes collapsed strip has real layout room");
        require(panel.getLocalBounds().contains(view->getBounds()),"V notes strip inside parent");
        require(comparison && comparison->isVisible() && !comparison->getBounds().intersects(view->getBounds()),
            "collapsed notes do not cover comparison");
        auto* toggle=dynamic_cast<juce::Button*>(view->findChildWithID("reference-change-notes-toggle"));
        auto* notesList=dynamic_cast<juce::ListBox*>(view->findChildWithID("reference-change-notes-list"));
        require(toggle && notesList, "change notes have local controls");
        const auto original=notes->notes[0].text;
        auto longNotes=std::make_shared<reference_audition::ChangeNotes>(*notes);
        longNotes->notes[0].text=juce::String::repeatedString(original.upToFirstOccurrenceOf("\n",false,false)+" ",20)+"TAIL";
        state.changeNotes=longNotes; panel.setState(state);
        toggle->onClick();
        require(notesList->isVisible() && notesList->getWidth()>=view->getWidth()-12,
            "V notes list uses the full pane at every width");
        require(notesList->getRowHeight()>24 && notesList->getModel()->getNameForRow(0).endsWith("TAIL"),
            "long note wraps to a taller row and keeps its tail");
        toggle->onClick();
        state.changeNotes=notes; panel.setState(state);
        const auto save=[&](const juce::String& stage){
            if(preview.isEmpty())return;
            const auto path=juce::File(preview).getChildFile("notes-surface-"+juce::String(language==i18n::Language::japanese?"ja-":"en-")+juce::String(width)+"-"+stage+".png");
            require(!path.exists(),"fresh surface image"); auto out=path.createOutputStream();
            require(out && juce::PNGImageFormat().writeImageToStream(shell.createComponentSnapshot(shell.getLocalBounds()),*out),"surface image write");++images;
        };
        save("marks");
        require(view->selectNote(0),"local note selection");
        std::cout<<"NOTES_EXPANDED width="<<width<<" height="<<view->getHeight()<<" comparison="<<comparison->isVisible()<<std::endl;
        require(view->isExpanded() && view->getHeight()>=90 && !comparison->isVisible(),"expanded notes replace comparison with readable pane");
        require(view->displayedBody()==notes->notes[0].text && audioCommands==0,"opening note preserves exact body without audio commands");
        save("body");
        auto* back=dynamic_cast<juce::Button*>(view->findChildWithID("reference-change-notes-toggle"));
        require(back && back->isVisible(),"expanded pane has a visible way back");
        back->onClick(); back->onClick();
        require(!view->isExpanded() && comparison->isVisible() && view->displayedBody().isEmpty(),"two local back actions restore comparison without retaining text");
        require(view->selectNote(0) && audioCommands==0,"repeated open remains presentation only");
        // Exercise the actual parent's expansion callback while replacing an open V.
        // Count ListBoxModel rows, not noteCount(): duplicate presentation indices
        // can coexist with an otherwise correct immutable notes receipt.
        auto* list=dynamic_cast<juce::ListBox*>(view->findChildWithID("reference-change-notes-list"));
        auto* body=dynamic_cast<juce::TextEditor*>(view->findChildWithID("reference-change-notes-body"));
        require(list && list->getModel() && body,"real notes list and body exist");
        for(int revision=1;revision<=3;++revision) {
            const auto oldText=view->displayedBody();
            require(view->isExpanded() && oldText.isNotEmpty(),"replacement starts with an open old note");
            auto replacement=std::make_shared<reference_audition::ChangeNotes>(*notes);
            replacement->artifactSha256="replacement-"+juce::String(revision);
            replacement->notes[0].noteId="replacement-first-"+juce::String(revision);
            replacement->notes[0].text="New version note "+juce::String(revision);
            auto completed=replacement->notes[0]; completed.noteId="replacement-done-"+juce::String(revision);
            completed.number=2; completed.done=true; completed.text="Completed note "+juce::String(revision);
            replacement->notes.push_back(completed);
            state.versionId="replacement-version-"+juce::String(revision); state.changeNotes=replacement;
            panel.setState(state);
            std::cout<<"NOTES_VERSION_REPLACEMENT width="<<width<<" revision="<<revision
                <<" rows="<<list->getModel()->getNumRows()<<" notes="<<replacement->notes.size()<<std::endl;
            require(list->getModel()->getNumRows()==static_cast<int>(replacement->notes.size()),
                "new V receipt has exactly one list row per note, without callback reentry duplicates");
            require(!view->isExpanded() && view->displayedBody().isEmpty() && !body->isVisible()
                && body->getTitle().isEmpty() && body->getDescription().isEmpty(),
                "new V closes old body and clears its accessible text");
            require(comparison->isVisible() && audioCommands==0,"new V restores comparison without audio commands");
            for(int cycle=0;cycle<2;++cycle) {
                require(view->selectNote(0) && view->displayedBody()==replacement->notes[0].text
                    && !view->displayedBody().contains(oldText),"reopening shows only the new V note");
                require(!comparison->isVisible(),"new V open body replaces comparison");
                back->onClick(); back->onClick();
                require(!view->isExpanded() && view->displayedBody().isEmpty() && comparison->isVisible()
                    && list->getModel()->getNumRows()==static_cast<int>(replacement->notes.size()) && audioCommands==0,
                    "repeated local Back preserves unique list and restores comparison without audio commands");
            }
            require(view->selectNote(0),"leave new V open for next replacement or Blind");
        }
        state.blindPhase=reference_ui::BlindPhase::active; panel.setState(state);
        require(!view->isVisible() && view->noteCount()==0 && view->displayedBody().isEmpty(),"actual V surface hides and clears notes in Blind");
        text_style::ShownTextLog log;
        shell.createComponentSnapshot(shell.getLocalBounds());
        require(!log.texts().joinIntoString(" ").contains(i18n::tr("Change notes")),"Blind image contains no notes heading");
        save("blind");
        state.blindPhase=reference_ui::BlindPhase::available; state.changeNotes.reset(); panel.setState(state);
        require(!view->isVisible() && view->displayedBody().isEmpty() && audioCommands==0,"notes removal cannot restore old body or issue audio commands");
    }
    std::cout<<"REFERENCE_CHANGE_NOTES_SURFACE_CASES=6 IMAGES="<<images<<'\n';
}
}
