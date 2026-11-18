#pragma once
#include "BlindPreferenceComponentTest.h"
namespace hypha::tests
{
template<typename Require, typename Fits>
void verifyBlindGainResult(Require require,Fits fits,const juce::File& root,const juce::String& preview)
{
    using blind_preference_component_test::Harness;
    for(auto language:{i18n::Language::english,i18n::Language::japanese}) {
        const i18n::ScopedLanguage scoped(language);
        for(bool candidate:{false,true}) for(int rounds:{1,5,10}) for(int width:{300,600,900}) {
            Harness h(rounds,candidate,root.getChildFile("gain-result-memory-only"));
            h.view.setSize(width,width*2/3);
            live_compare::GainSnapshot gain; gain.coherent=true; gain.retained=true; gain.revision=2;
            gain.pre=static_cast<float>(std::pow(10.0,1.2/20.0));
            gain.post=rounds==1?1.0f:static_cast<float>(std::pow(10.0,-2.3/20.0));
            gain.ceiling=.9f; gain.identity={1,2,3,4,48000};
            h.view.setPreferenceApprovedGains(h.status.generation,gain);
            require(h.view.screen().matchedGainText.isEmpty(),"gain remains hidden before REVEAL");
            if(rounds>1) for(int i=0;i<rounds;++i) { h.heard(3); h.choose((h.accepted+1)%3!=0?1:2); }
            h.heard(3); h.reveal();
            blind_preference_component_test::printResultGeometry(h.view,
                juce::String(candidate?"AB ":"PREPOST ")+(language==i18n::Language::japanese?"JA ":"EN ")
                +juce::String(width)+" rounds="+juce::String(rounds));
            if(preview.isNotEmpty()) {
                const auto file=juce::File(preview).getChildFile("gain-result-"+juce::String(candidate?"ab-":"prepost-")
                    +(language==i18n::Language::japanese?"ja-":"en-")+juce::String(width)+"-"+juce::String(rounds)+".png");
                require(!file.exists(),"gain result image is fresh");
                require(capture::writePng(h.view.createComponentSnapshot(h.view.getLocalBounds()),file)==capture::PngSave::saved,"gain result image saved");
            }
            const auto text=h.view.screen().matchedGainText;
            const auto nameA=candidate?"A":"PRE",nameB=candidate?"B":"POST";
            require(text.contains(juce::String(nameA)+" +1.2 dB")
                && text.contains(juce::String(nameB)+(rounds==1?" 0.0 dB":juce::String::fromUTF8(u8" −2.3 dB"))),
                "approved result gain shows PREPOST or AB, signed nonzero and unsigned zero");
            require(!h.view.findChildWithID("live-blind-details") && !h.view.findChildWithID("live-blind-details-viewport"),
                "removed Details has no remaining controls");
            auto* body=dynamic_cast<juce::TextEditor*>(h.view.findChildWithID("live-blind-matched-gain"));
            require(body && body->isVisible() && body->isReadOnly() && body->getFont().getHeight()>=12,"gain line readable");
            require(body->getJustificationType().testFlags(juce::Justification::horizontallyCentred),"gain line centred beneath result");
            auto* note=dynamic_cast<juce::TextEditor*>(h.view.findChildWithID("live-blind-preference-note"));
            auto* result=dynamic_cast<juce::TextEditor*>(h.view.findChildWithID("live-blind-preference-result"));
            require(note && note->isVisible() && body->getBottom()<=note->getY(),"compensation precedes explanatory notes");
            if(rounds==1) {
                require(result && !result->isVisible(),"one listen has no invented main preference result");
                require(note->getText().contains(text_style::shownText(h.view.screen().instruction))
                    && note->getText().contains(text_style::shownText(h.view.screen().detail)),"both one-listen notes follow compensation");
            } else require(result && result->isVisible() && result->getBottom()<=body->getY(),"voted result precedes compensation");
            if(rounds>1) require(result->getTextHeight()+4<=result->getHeight(),"entire main result and probability fit without scrolling");
            const auto fullTextVisible=[&](juce::TextEditor& editor) {
                juce::Viewport* viewport=nullptr;
                for(auto* child:editor.getChildren()) if(auto* v=dynamic_cast<juce::Viewport*>(child)) viewport=v;
                require(viewport && viewport->getViewedComponent(),"text has actual viewport");
                require(viewport->getViewPosition()==juce::Point<int>{},"all text visible without scrolling");
                const auto visible=editor.getLocalArea(viewport,viewport->getLocalBounds());
                const auto bounds=editor.getTextBounds({0,editor.getText().length()});
                require(!bounds.isEmpty(),"nonempty rendered text bounds checked");
                for(const auto& rectangle:bounds)
                    require(visible.contains(rectangle),"every rendered text fragment fits horizontally and vertically");
                require(editor.getTextHeight()+4<=editor.getHeight(),"complete wrapped body height fits");
            };
            fullTextVisible(*note); fullTextVisible(*body);
            if(rounds>1) fullTextVisible(*result);
            if(width>=600 && rounds>1) {
                const auto group=result->getBounds().getUnion(body->getBounds());
                const int availableTop=24+40;
                require(std::abs(group.getCentreY()-(availableTop+note->getY())/2)<=1,
                    "large result and smaller gain are centred above bottom notes");
                require(result->getFont().getHeight()>body->getFont().getHeight(),"result remains larger than compensation");
            }
            fits(h.view,"gain result");
            require(body->getTextHeight()+4<=body->getHeight(),"complete gain line fits without scrolling");
            require(h.view.getLocalBounds().contains(body->getBounds()),"gain remains within result");
            for(auto* child:h.view.getChildren()) if(child!=body && child->isVisible())
                require(!body->getBounds().intersects(child->getBounds()),"gain never overlaps other controls");
            const auto layout=blind_image::layoutFor(h.view.resultFacts());
            juce::String saved; for(const auto& item:layout.items) saved+=item.text;
            require(!saved.contains("+1.2") && !saved.contains(juce::String::fromUTF8(u8"−2.3")),"saved image excludes compensation gains");
            h.view.setCovered(true); require(!body->isVisible(),"image sheet covers gain");
            h.view.setCovered(false); require(body->isVisible(),"gain returns after sheet closes");
            auto changed=gain; ++changed.revision;
            h.view.setPreferenceApprovedGains(h.status.generation,changed);
            require(h.view.screen().matchedGainText.isEmpty(),"changed approval cannot replace original gain");
            changed=gain; ++changed.identity.ownerA;
            h.view.setPreferenceApprovedGains(h.status.generation,changed);
            require(h.view.screen().matchedGainText.isEmpty(),"changed identity cannot replace original gain");
            h.view.setPreferenceApprovedGains(h.status.generation,gain);
            require(h.view.screen().matchedGainText==text,"same approved tuple can revalidate");
            h.view.setPreferenceApprovedGains(h.status.generation+1,gain);
            require(h.view.screen().matchedGainText.isEmpty(),"foreign generation excluded");
            h.view.setPreferenceApprovedGains(h.status.generation,std::nullopt);
            require(h.view.screen().matchedGainText.isEmpty(),"missing approval excluded");
            h.status.stage=live_compare::BlindStage::idle; h.publish();
            require(h.view.screen().matchedGainText.isEmpty(),"END clears gain");
        }
    }
    Harness late(1,false,root.getChildFile("late-gain-memory-only"));
    late.heard(3); late.reveal();
    live_compare::GainSnapshot receipt; receipt.coherent=true; receipt.retained=true;
    receipt.pre=receipt.post=receipt.ceiling=1; receipt.revision=1; receipt.identity={1,2,3,4,48000};
    late.view.setPreferenceApprovedGains(late.status.generation,receipt);
    require(late.view.screen().matchedGainText.isEmpty(),"first receipt after REVEAL cannot fabricate the trial gain");
}
}
