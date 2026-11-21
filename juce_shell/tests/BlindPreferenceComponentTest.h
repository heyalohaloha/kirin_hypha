#pragma once
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaBlindResultImage.h"
#include "../src/HyphaBlindPreferenceText.h"
#include "../src/HyphaCapturePngFile.h"
#include <stdexcept>
#include <cmath>
#include <iostream>
#include "BlindLongNameResultTest.h"
namespace hypha::tests::blind_preference_component_test
{
inline void printResultGeometry(juce::Component& view,const juce::String& phase)
{
    std::cout << "BLIND_RESULT_GEOMETRY " << phase << " view=" << view.getBounds().toString() << std::endl;
    int textHeight=0, allocated=0;
    for(auto* child:view.getChildren()) if(child->isVisible()) {
        std::cout << "  " << child->getComponentID() << " bounds=" << child->getBounds().toString();
        if(auto* editor=dynamic_cast<juce::TextEditor*>(child)) {
            textHeight+=editor->getTextHeight()+4; allocated+=editor->getHeight();
            std::cout << " font=" << editor->getFont().getHeight() << " textHeight=" << editor->getTextHeight()
                << " text=" << editor->getText().replaceCharacter('\n','|');
        }
        std::cout << std::endl;
    }
    std::cout << "  requiredTextWithPadding=" << textHeight << " allocatedBodies=" << allocated << std::endl;
}
struct Harness
{
    live_blind_ui::Component view;
    live_compare::LiveBlindStatus status;
    unsigned accepted = 0, nextCalls = 0;
    bool randomFails = false, rejectsAnswer = false;
    explicit Harness (int rounds, bool candidate, const juce::File& preferenceFile)
    {
        view.setPreferenceFile(preferenceFile); view.setPreferenceRounds(rounds);
        status.stage = live_compare::BlindStage::active; status.generation = 17;
        status.trial.epoch = 6; status.trial.selected = 2; status.candidate = candidate;
        view.onPreferenceAnswer = [this] (int source, std::uint64_t generation, std::uint32_t epoch) -> std::optional<bool> {
            if (rejectsAnswer || generation != status.generation || epoch != status.trial.epoch
                || status.trial.played != 3 || status.trial.revealed || (source != 1 && source != 2)) return {};
            ++accepted;
            // Hidden independent sequence deliberately repeats, rather than alternates.
            const bool firstA = accepted % 3 != 0;
            return (source == 1) == firstA;
        };
        view.onPreferenceNext = [this] (std::uint64_t generation, std::uint32_t epoch) {
            ++nextCalls;
            if (generation != status.generation || epoch != status.trial.epoch) return false;
            if (randomFails) { status.stage = live_compare::BlindStage::failed; publish(); return false; }
            ++status.trial.epoch; status.trial.played = 0;
            publish(); // Exercise reentrant status refresh inside the callback.
            return true;
        };
        publish();
    }
    void publish() { view.setState(status,true,1.0f); }
    void heard (int mask) { status.trial.played = mask; publish(); }
    void choose (int source) { view.onGuess(source); }
    void reveal() { status.trial.revealed = true; publish(); }
};

template <typename Require>
void verifySmallSources (Harness& h, Require require, const juce::String& where, int minimum, bool exerciseClick)
{
    auto& view = h.view;
    int selected = 0;
    view.onSelect = [&] (int source) { selected = source; };
    for (const auto source : {1, 2})
    {
        auto* card = dynamic_cast<juce::Button*>(view.findChildWithID ("live-blind-source-" + juce::String (source)));
        if (!card) { require(false, where + " missing SOURCE"); continue; }
        const auto bounds = card->getBounds();
        require(card->isVisible() && card->isEnabled() && bounds.getHeight() >= minimum && !bounds.isEmpty()
                && view.getLocalBounds().contains(bounds), where + " SOURCE size: " + bounds.toString());
        for (auto* sibling : view.getChildren())
            if (sibling != card && sibling->isVisible())
                require(!bounds.intersects(sibling->getBounds()), where + " SOURCE overlaps " + sibling->getComponentID());
        const auto centre = bounds.getCentre();
        require(view.getComponentAt(centre) == card, where + " SOURCE centre is mouse-reachable");
        if (exerciseClick)
        {
            card->onClick();
            require(selected == source, where + " SOURCE click reaches the selector");
        }
    }
    view.onSelect = {};
}

template <typename Require>
void verifySmallPreferenceLayout (Require require, const juce::File& root, const juce::String& preview, const juce::String& lang)
{
    const auto legacy = root.getChildFile("preference-small-legacy");
    require(blind_image::preferGuess(legacy,true)
            && ui_preferences::read(legacy,"blind-rounds").isEmpty(), "upgrade fixture has only the old GUESS key");
    for (bool candidate : {false,true}) for (int width : {375,450}) for (int rounds : {1,5,10})
        for (bool migrated : {false,true})
    {
        if (migrated && (candidate || width != 450 || rounds != 5)) continue;
        const auto settings = migrated ? legacy : root.getChildFile("preference-small-default");
        const auto choice = migrated ? blind_image::preferenceRounds(settings) : rounds;
        Harness h(choice,candidate,settings);
        h.view.setBounds(0,0,width,width*2/3);
        h.view.setVisible(true);
        const auto tag = juce::String("small preference ") + lang + " " + juce::String(width)
            + " x" + juce::String(rounds) + (candidate ? " A/B" : " PRE/POST")
            + (migrated ? " migrated" : "");
        require(h.view.activePreferenceRounds()==rounds,tag + " exact round count");
        const int minimum = 60;
        verifySmallSources(h,require,tag + " before a vote",minimum,true);
        if (width == 450)
        {
            auto* selector = h.view.findChildWithID("live-blind-rounds-5");
            auto* end = dynamic_cast<juce::Button*>(h.view.findChildWithID("live-blind-end"));
            require(selector && selector->isVisible() && !selector->getBounds().isEmpty(),tag + " retained selector row");
            auto* reveal = h.view.findChildWithID("live-blind-reveal");
            auto* vote = h.view.findChildWithID("live-blind-guess-1");
            require(end && reveal && vote && selector->getHeight()==end->getHeight()
                && reveal->getHeight()==end->getHeight() && (rounds==1 || vote->getHeight()==end->getHeight()),
                tag + " equal action heights at 450 without shrinking sources");
            require(end && end->getDescription().contains(h.view.screen().detail)
                    && end->getTooltip().contains(h.view.screen().detail),tag + " END preserves explanation");
            if (!candidate && !migrated && rounds != 10 && preview.isNotEmpty())
            {
                const auto png = juce::File(preview).getChildFile("blind-450-" + lang + "-" + juce::String(rounds) + ".png");
                png.deleteFile();
                require(capture::writePng(h.view.createComponentSnapshot(h.view.getLocalBounds()),png)==capture::PngSave::saved,
                        tag + " preview saved");
            }
        }
        h.heard(1);
        if (rounds != 1) {
            h.choose(1);
            require(h.accepted==0 && !h.view.screen().guessOpen,tag + " one heard source cannot vote");
        }
        h.heard(3);
        if (rounds != 1) {
            h.choose(1);
            require(h.accepted==1 && h.view.preferenceTrial().roundNumber()==2,tag + " next round after one accepted vote");
            verifySmallSources(h,require,tag + " next round",minimum,true);
        }
        h.reveal();
        // The RESULT screen keeps its established layout: 40 px sources, or one 24 px row when dense.
        verifySmallSources(h,require,tag + " after REVEAL",24,true);
    }
    require(ui_preferences::read(legacy,"blind-rounds").isEmpty(), "migration does not populate a new round setting");
    require(legacy.deleteFile(), "upgrade fixture removed");
}
}
namespace hypha::tests
{
template <typename Require, typename Fits>
void verifyBlindPreferenceComponent (Require require, Fits fits, const juce::File& root, const juce::String& preview)
{
    using namespace blind_preference_component_test;
    const auto file = root.getChildFile("preference-memory-only");
    const auto filesBefore = root.findChildFiles(juce::File::findFilesAndDirectories,true).size();
    for (int rounds : {5,10}) for (bool candidate : {false,true}) {
        Harness h(rounds,candidate,file);
        for (int i=0; i<rounds; ++i) {
            require(h.view.preferenceTrial().roundNumber()==unsigned(i+1),"component round number advances");
            require(!h.view.screen().guessOpen,"round begins unheard"); h.choose(1);
            require(h.accepted==unsigned(i),"unheard choice never reaches authority");
            h.heard(1); require(!h.view.screen().guessOpen,"one side cannot answer");
            h.heard(3); require(h.view.screen().guessOpen,"both actual sides allow answer");
            h.rejectsAnswer=true; h.choose(1); h.rejectsAnswer=false;
            require(h.view.trialSummary().total()==unsigned(i),"authority rejection records no vote");
            h.choose(1);
            require(h.view.trialSummary().total()==unsigned(i+1),"one accepted vote per round");
            require(h.status.trial.selected==2,"round preserves anonymous selected source");
            require(h.view.screen().revealEnabled,"early reveal stays enabled after accepted answer");
            if (i+1<rounds) require(h.view.preferenceTrial().playedMask()==0,"reentrant next clears played receipts");
        }
        require(h.nextCalls==unsigned(rounds-1) && !h.status.trial.revealed,"last answer does not auto reveal or rerandomise");
        h.choose(2); require(h.accepted==unsigned(rounds),"completion refuses extra vote");
        h.reveal();
        require(!h.view.screen().guessShown && h.view.screen().preferenceResultText.isNotEmpty(),"result uses independent pane not old answer row");
        require(h.view.resultFacts().preference.total()==unsigned(rounds),"SAVE IMAGE gets exact answered count");
        h.status.stage=live_compare::BlindStage::idle; h.publish();
        require(h.view.trialSummary().total()==0,"END clears memory and details");
    }
    {
        Harness h(5,false,file); h.heard(3); h.choose(2); h.heard(1); h.reveal();
        require(h.view.trialSummary().total()==1 && !h.view.trialSummary().chance.available,"partial reveal counts no unanswered round or probability");
        require(!h.view.screen().preferenceResultText.containsChar('\n'),"one answer has only result line");
    }
    {
        Harness h(1,false,file);
        require(h.view.preferenceRounds()==1 && !h.view.screen().guessShown,"one round is default legacy listen");
        int changed=0; h.view.onPreferenceRoundsChanged=[&](int n){changed=n;};
        h.view.onRounds(5);
        require(changed==5 && h.view.activePreferenceRounds()==5 && h.view.preferenceTrial().planned()==5,
                "screen setter and menu accessor agree before answer");
        require(h.view.setPreferenceRounds(10) && h.view.activePreferenceRounds()==10,"answer-free round count changes immediately");
        h.heard(3); h.choose(1);
        require(!h.view.canChangePreferenceRounds() && !h.view.setPreferenceRounds(1)
            && h.view.preferenceRounds()==10 && h.view.trialSummary().total()==1,"answered trial rejects changes without resetting votes");
        h.view.onRounds(5); require(changed==5 && h.view.preferenceRounds()==10,"disabled selector sends no setting change");
        require(!h.view.screen().roundsEnabled && h.view.screen().roundsProgress==blind_preference::roundProgress(2,10),"answered selector becomes progress only");
        h.reveal(); require(!h.view.setPreferenceRounds(5),"result cannot retroactively change round target");
        h.status.stage=live_compare::BlindStage::idle; h.status.trial.revealed=false; h.publish();
        require(h.view.canChangePreferenceRounds() && h.view.setPreferenceRounds(1),"after END next trial can select again");
        ++h.status.generation; h.status.stage=live_compare::BlindStage::active; h.publish();
        require(h.view.activePreferenceRounds()==1 && !h.view.screen().guessShown,"next trial captures one-round choice");
    }
    {
        Harness h(5,false,file); h.heard(3); h.choose(1);
        h.status.trial.epoch+=4; h.publish();
        require(h.view.trialSummary().total()==0,"unexpected epoch clears history");
        ++h.status.generation; h.publish(); h.heard(3); h.randomFails=true; h.choose(2);
        require(h.nextCalls==2 && h.view.trialSummary().total()==0 && !h.view.screen().guessOpen,"random failure ends without fabricated assignment or restored history");
    }
    for (bool candidate : {false,true}) for (bool revealed : {false,true}) {
        Harness off(1,candidate,file); off.status.trial.revealed=revealed; off.heard(3);
        auto one=off.view.screen(); one.roundsShown=false; one.roundsEnabled=false; one.roundsValue=1; one.roundsProgress.clear();
        auto legacy=live_blind_ui::screenFor(off.status,true,1.0f); legacy.roundsValue=1;
        require(one==legacy,"one round preserves legacy no-question screen except the new selector");
    }
    require(!file.exists() && root.findChildFiles(juce::File::findFilesAndDirectories,true).size()==filesBefore,"rounds/answers/mappings never write preferences or history files");
    for (auto language : {i18n::Language::english,i18n::Language::japanese}) {
        const i18n::ScopedLanguage shown(language);
        const auto lang = language==i18n::Language::english ? "en" : "ja";
        verifySmallPreferenceLayout(require,root,preview,lang);
        for (const auto counts : {std::pair<unsigned,unsigned>{5,0},{4,1},{3,2},{10,0},{9,1},{8,2},{7,3},{6,4},{5,5}}) {
            const blind_preference::Summary summary{counts.first,counts.second,blind_preference::probability(counts.first,counts.second)};
            require(blind_preference::probabilityLine(summary).isEmpty()==(summary.chance.roundedPerHundred()==100),
                "rounded 100 percent is hidden; other probabilities remain visible");
        }
        for (bool candidate : {false,true}) {
            const auto namesFile=root.getChildFile("preference-named-fixture");
            const auto a=language==i18n::Language::japanese ? juce::String::fromUTF8(u8"処理前") : juce::String("Before");
            const auto b=language==i18n::Language::japanese ? juce::String::fromUTF8(u8"処理後") : juce::String("After");
            require(blind_image::rememberName(namesFile,candidate,false,a)
                && blind_image::rememberName(namesFile,candidate,true,b),"named fixture settings prepared");
            Harness named(5,candidate,namesFile); named.heard(3); named.choose(1); named.reveal();
            require(named.view.screen().preferenceResultText.contains(a)
                && named.view.screen().sourceOne.contains(b) && named.view.screen().sourceTwo.contains(a),
                "named revealed cards and preference summary use the same names");
            require(namesFile.deleteFile(),"named fixture settings removed");
        }
        for (bool candidate : {false,true}) for (int width : {300,600,900}) {
            Harness h(5,candidate,file); h.view.setBounds(0,0,width,width*2/3);
            live_compare::GainSnapshot gain;
            gain.coherent=true; gain.retained=true; gain.revision=2;
            gain.pre=static_cast<float>(std::pow(10.0,1.2/20.0));
            gain.post=static_cast<float>(std::pow(10.0,-2.3/20.0));
            gain.ceiling=0.9f; gain.identity={1,2,3,4,48000};
            h.view.setPreferenceApprovedGains(h.status.generation,gain); // before the first answer
            const auto save = [&] (const char* phase) {
                printResultGeometry(h.view,juce::String("PREFERENCE ")+lang+" "+phase+" "+juce::String(width));
                for (int i=0;i<h.view.getNumChildComponents();++i) {
                    auto* child=h.view.getChildComponent(i);
                    if (child->isVisible()) if (auto* editor=dynamic_cast<juce::TextEditor*>(child)) {
                        require(editor->getFont().getHeight()>=12.0f,"result and details body is at least 12px");
                        if (requiresJapaneseGlyphs(editor->getText()))
                            require(editor->getFont().getTypefaceName()==nativeTextFontLike(editor->getFont()).getTypefaceName(),
                                "Japanese result/details TextEditor uses the native Japanese font");
                    }
                }
                if (preview.isNotEmpty()) {
                    const auto png=juce::File(preview).getChildFile("preference-"+juce::String(candidate?"ab-":"prepost-")
                        +lang+"-"+juce::String(width)+"-"+phase+".png");
                    png.deleteFile();
                    require(capture::writePng(h.view.createComponentSnapshot(h.view.getLocalBounds()),png)==capture::PngSave::saved,"preference component preview saved");
                }
                fits(h.view,juce::String("PREFERENCE ")+lang+" "+phase+" "+juce::String(width));
            };
            const auto childById = [&] (const char* id) -> juce::Component* {
                const auto find = [&] (auto&& self, juce::Component& parent) -> juce::Component* {
                    for (auto* child : parent.getChildren()) {
                        if (child->getComponentID()==id) return child;
                        if (auto* found=self(self,*child)) return found;
                    }
                    return nullptr;
                };
                return find(find,h.view);
            };
            const auto selector = childById("live-blind-rounds-5");
            const auto progress = childById("live-blind-rounds-label");
            require(selector && progress && selector->isVisible()==(width>=450),"selector is available at 600/900 and hidden at 300");
            auto* count = dynamic_cast<juce::Label*>(progress);
            auto* prefer = dynamic_cast<juce::Label*>(childById("live-blind-guess-question"));
            require(h.view.screen().guessQuestionFixed && count && count->getText()=="ROUNDS"
                && prefer && prefer->getText()=="Which do you prefer?",
                "fixed selector and choice prompt keep English in both languages");
            for (int value : { 1, 5, 10 }) {
                const auto id = "live-blind-rounds-" + juce::String(value);
                auto* option = dynamic_cast<HyphaTextButton*>(childById(id.toRawUTF8()));
                require(option && option->displayedText()==juce::String(value)+juce::String::fromUTF8(u8"×"),
                    "fixed count control keeps its numeric mark");
            }
            const auto verifyCoverRoundTrip = [&] (bool answered) {
                auto* one=childById("live-blind-rounds-1");
                auto* ten=childById("live-blind-rounds-10");
                auto* outcome=childById("live-blind-guess-outcome");
                require(one && ten && outcome,"covered round controls exist");
                h.view.setCovered(true);
                require(!selector->isVisible() && !progress->isVisible()
                    && !one->isVisible() && !ten->isVisible(),"covered immediately hides every round control");
                h.view.resized();
                h.view.setCovered(false);
                const bool large=width>=450;
                require(progress->isVisible()==large && selector->isVisible()==(large && !answered)
                    && one->isVisible()==(large && !answered) && ten->isVisible()==(large && !answered),
                    "uncover restores selector or progress; compact stays hidden");
                require(outcome->isVisible()==!large,"uncover keeps only compact inline progress");
            };
            verifyCoverRoundTrip(false);
            save("selector");
            h.heard(3);
            auto* answerOne=dynamic_cast<HyphaTextButton*>(childById("live-blind-guess-1"));
            auto* sourceOne=childById("live-blind-source-1");
            auto* reveal=dynamic_cast<HyphaTextButton*>(childById("live-blind-reveal"));
            auto* end=childById("live-blind-end");
            require(answerOne && sourceOne && reveal && end && answerOne->isEnabled()
                && answerOne->findColour(juce::TextButton::textColourOffId)==COL_NORMAL,
                "available preference choice contrasts with disabled lettering");
            require(sourceOne->getHeight()>=60 && answerOne->getHeight()==end->getHeight()
                && reveal->getHeight()==end->getHeight()
                && (!selector->isVisible() || selector->getHeight()==end->getHeight()),
                "source cards retain their minimum; other interactive rows use one common height");
            require(reveal->displayedText()=="REVEAL","Reveal stays English in both languages");
            h.choose(1); h.heard(3); h.choose(2);
            require(h.view.preferenceTrial().roundNumber()==3 && !selector->isVisible(),"answered selector buttons replaced by progress");
            require(progress->isVisible()==(width>=450),"small-size progress fallback does not overlap");
            if (auto* label=dynamic_cast<juce::Label*>(progress); label && label->isVisible())
                require(label->getFont().getHeight()>=12.0f,"progress text remains at least 12px");
            verifyCoverRoundTrip(true);
            save("round3"); h.reveal(); save("result");
            // A fresh generation gives five accepted A votes without exposing hidden mapping.
            h.status.stage=live_compare::BlindStage::idle; h.publish();
            ++h.status.generation; ++h.status.trial.epoch; h.status.trial.revealed=false;
            h.accepted=0; h.nextCalls=0; h.status.stage=live_compare::BlindStage::active; h.publish();
            for (unsigned vote=0;vote<5;++vote) {
                h.heard(3);
                const bool firstA=(h.accepted+1)%3!=0;
                h.choose(firstA?1:2);
            }
            require(h.accepted==5 && h.view.trialSummary().a==5 && h.view.trialSummary().b==0,"majority fixture has five accepted A answers");
            h.reveal();
            const auto summary=blind_preference::summaryLine(h.view.trialSummary(),candidate);
            const auto chance=blind_preference::probabilityLine(h.view.trialSummary());
            require(summary.isNotEmpty() && chance.isNotEmpty()
                && h.view.screen().preferenceResultText==summary+"\n"+chance,
                "majority result contains both complete result and nonempty probability");
            save("result-majority"); // Preserve the diagnostic image even if visibility fails below.
            auto* result=dynamic_cast<juce::TextEditor*>(childById("live-blind-preference-result"));
            require(result && result->isVisible(),"majority result pane is visible");
            if (result) {
                juce::Viewport* viewport=nullptr;
                for(int i=0;i<result->getNumChildComponents();++i)
                    if(auto* v=dynamic_cast<juce::Viewport*>(result->getChildComponent(i))) viewport=v;
                require(viewport && viewport->getViewedComponent(),"majority result viewport is measurable");
                if(viewport && viewport->getViewedComponent()) {
                    const auto visible=result->getLocalArea(viewport,viewport->getLocalBounds());
                    const auto first=result->getCaretRectangleForCharIndex(0);
                    const auto last=result->getCaretRectangleForCharIndex(result->getText().length()-1);
                    require(viewport->getViewPositionY()==0
                        && viewport->getViewedComponent()->getHeight()<=viewport->getViewHeight()+2
                        && first.getY()>=visible.getY() && last.getBottom()<=visible.getBottom(),
                        "complete majority result and probability fit without vertical scrolling at every size");
                }
            }
        }
        for (bool candidate : {false,true}) for (const auto counts : {std::pair<unsigned,unsigned>{1,0},{5,0},{8,2},{5,5}}) {
            blind_image::Facts facts; facts.candidate=candidate;
            const bool named = counts.first==8 && counts.second==2;
            if (named) {
                facts.nameA=language==i18n::Language::japanese ? juce::String::fromUTF8(u8"処理前") : "Before";
                facts.nameB=language==i18n::Language::japanese ? juce::String::fromUTF8(u8"処理後") : "After";
            }
            if (!named) { facts.nameA=blind_image::defaultName(candidate,false); facts.nameB=blind_image::defaultName(candidate,true); }
            facts.preference={counts.first,counts.second,blind_preference::probability(counts.first,counts.second)};
            const auto layout=blind_image::layoutFor(facts);
            juce::String text;
            for (std::size_t i=0;i<layout.items.size();++i) {
                const auto& item=layout.items[i]; text+=item.text;
                require(item.font.getHeight()*2.0f>=24.0f && item.font.getStringWidthFloat(item.text)<=item.area.getWidth()+0.5f,"preference image text fits at 12px minimum");
                for (std::size_t j=i+1;j<layout.items.size();++j)
                    require(!item.area.intersects(layout.items[j].area),"preference image lines never overlap");
            }
            if (named) require(text.contains(facts.nameA) && text.contains(facts.nameB),
                "named preference image preserves both supplied display names");
            const auto plain=[](juce::String x){return x.removeCharacters(" \n\r");};
            require(plain(text).contains(plain(blind_preference::summaryLine(facts.preference,candidate,facts.nameA,facts.nameB,true))),"image includes exact preference summary");
            if (blind_preference::probabilityLine(facts.preference).isNotEmpty())
                require(plain(text).contains(plain(blind_preference::probabilityLine(facts.preference))),"image includes exact two-sided probability text");
            if (language == i18n::Language::japanese)
                require(!blind_preference::probabilityLine(facts.preference).contains("about")
                    && !blind_preference::probabilityLine(facts.preference).contains(" in "),
                    "specific Japanese chance template is not swallowed by generic pattern");
            if (preview.isNotEmpty() && facts.preference.total()>=5) {
                const auto png=juce::File(preview).getChildFile("preference-image-"+juce::String(candidate?"ab-":"prepost-")+lang
                    +"-"+juce::String(counts.first)+"-"+juce::String(counts.second)+".png");
                png.deleteFile();
                require(capture::writePng(blind_image::render(facts),png)==capture::PngSave::saved,"preference image preview saved");
            }
        }
    }
    verifyBlindLongNameResult (require, preview);
    require(!file.exists(),"details never writes preference history");
    std::cout << "Blind PREFERENCE component: PASS (state, next-setting, partial reveal, images, EN/JA x 300/600/900)\n";
}
}
