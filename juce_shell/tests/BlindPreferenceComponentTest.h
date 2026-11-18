#pragma once
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaBlindResultImage.h"
#include "../src/HyphaBlindPreferenceText.h"
#include "../src/HyphaCapturePngFile.h"
#include "BlindFrequencyRowTest.h"
#include <stdexcept>
#include <cmath>
namespace hypha::tests::blind_preference_component_test
{
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
}
namespace hypha::tests
{
template <typename Require, typename Fits>
void verifyBlindPreferenceComponent (Require require, Fits fits, const juce::File& root, const juce::String& preview)
{
    verifyBlindFrequencyRow (preview);
    using namespace blind_preference_component_test;
    const auto file = root.getChildFile("preference-memory-only");
    const auto filesBefore = root.findChildFiles(juce::File::findFilesAndDirectories,true).size();
    {
        Harness h(5,false,file);
        KirinSpectrumBatch batch{}; auto& frame=batch.latest;
        frame.status=KIRIN_SPECTRUM_ACTIVE; frame.has_data=frame.post_has_data=1;
        frame.sample_rate=48000; frame.channels=2; frame.channel_mode=KIRIN_SPECTRUM_CHANNEL_LR;
        frame.aperture_samples=4800; frame.presentation_end_samples=48000; frame.min_hz=20; frame.max_hz=20000;
        for(size_t i=0;i<KIRIN_SPECTRUM_BAND_COUNT;++i) {frame.pre_dbfs[i]=-60;frame.post_dbfs[i]=-50;frame.display_db[i]=2;}
        h.view.observePreferenceDetails(nullptr,nullptr,&batch,7);
        for(int n=0;n<30;++n) {
            for(auto& value:frame.display_db) value=n<20?2.0f:n<29?8.0f:14.0f;
            frame.presentation_end_samples+=4800; h.view.observePreferenceDetails(nullptr,nullptr,&batch,7);
        }
        require(!h.view.screen().frequencyDetails,"unrevealed live component exposes no frequency result");
        h.heard(3); h.choose(1); h.reveal(); h.view.onDetails();
        const auto frozen=h.view.screen().frequencyDetails;
        require(frozen && frozen->frequency[0].sufficient && frozen->frequency[0].median==2,
            "component freezes time median, distinct from mean and last frame");
        frame.presentation_end_samples+=4800; for(auto& value:frame.display_db) value=-20;
        h.view.observePreferenceDetails(nullptr,nullptr,&batch,7);
        require(h.view.screen().frequencyDetails==frozen && frozen->frequency[0].median==2,"frequency freeze rejects later frames");
        auto* viewport=h.view.findChildWithID("live-blind-details-viewport");
        require(viewport && viewport->isVisible(),"live frequency details viewport visible");
        h.view.setCovered(true); require(!viewport->isVisible(),"covered live details hides frequency");
        h.view.setCovered(false); require(viewport->isVisible(),"uncover preserves frozen details");
        h.status.stage=live_compare::BlindStage::idle; h.status.trial.revealed=false; h.publish();
        require(!h.view.screen().frequencyDetails,"END removes frequency result");
        ++h.status.generation; ++h.status.trial.epoch; h.status.stage=live_compare::BlindStage::active; h.publish();
        h.heard(3); h.choose(1); h.reveal(); h.view.onDetails();
        require(h.view.screen().frequencyDetails && !h.view.screen().frequencyDetails->hasFrequency,
            "new generation cannot reuse old frequency interval");
        Harness candidate(5,true,file); candidate.heard(3); candidate.choose(1); candidate.reveal(); candidate.view.onDetails();
        require(!candidate.view.screen().frequencyDetails,"A/B candidate details never expose PRE/POST frequency");
    }

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
        require(h.view.screen().detailsShown && !h.view.screen().detailsOpen,"details starts closed");
        h.view.onDetails(); require(h.view.screen().detailsOpen && h.view.screen().detailsText.isNotEmpty(),"explicit details shows history");
        h.status.stage=live_compare::BlindStage::idle; h.publish();
        require(h.view.trialSummary().total()==0 && !h.view.screen().detailsShown,"END clears memory and details");
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
        require(h.view.trialSummary().total()==0 && !h.view.screen().detailsShown,"unexpected epoch clears history");
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
    {
        const i18n::ScopedLanguage english(i18n::Language::english);
        Harness h(1,false,file);
        KirinReferenceLiveMetricFrame frame{};
        frame.version=1; frame.struct_size=sizeof frame; frame.sample_rate=48000; frame.channels=2;
        frame.measurement_epoch=10; frame.generation=20; frame.observed_frames=480000;
        frame.own_crest_db=8; frame.pre_crest_db=5; frame.own_psr_db=12; frame.pre_psr_db=10; frame.valid_mask=15;
        h.view.observePreferenceDetails(&frame,nullptr,nullptr,0); // BLIND-start baseline while set to 1
        require(h.view.setPreferenceRounds(5),"pre-answer 1 to 5 retains measurement interval");
        for(int i=1;i<=30;++i) {
            frame.observed_frames=480000+i*4800;
            h.view.observePreferenceDetails(&frame,nullptr,nullptr,0);
            h.view.observePreferenceDetails(&frame,nullptr,nullptr,0); // duplicate is not extra time
            if(i==15) { h.heard(3); h.choose(1); }
        }
        h.reveal(); h.view.onDetails();
        const auto frozen=h.view.screen().detailsText;
        require(frozen.contains("PSR: 2.00 dB") && frozen.contains("CREST: 3.00 dB"),
            "baseline plus thirty actual apertures survives count change and round boundary");
        require(frozen.contains("SHARP: Not shown because the measured time is too short"),"unobserved metric stays unavailable independently");
        frame.observed_frames+=4800; frame.own_psr_db=99;
        h.view.observePreferenceDetails(&frame,nullptr,nullptr,0);
        h.view.onDetails(); h.view.onDetails();
        require(h.view.screen().detailsText==frozen,"post-reveal observations cannot change frozen details");
        h.status.stage=live_compare::BlindStage::idle; h.status.trial.revealed=false; h.publish();
        ++h.status.generation; ++h.status.trial.epoch; h.status.stage=live_compare::BlindStage::active; h.publish();
        h.heard(3); h.choose(1); h.reveal(); h.view.onDetails();
        require(!h.view.screen().detailsText.contains("PSR: 2.00 dB")
            && h.view.screen().detailsText.contains("PSR: Not shown because the measured time is too short"),
            "END clears previous measured interval before next trial");
    }
    for (auto language : {i18n::Language::english,i18n::Language::japanese}) {
        const i18n::ScopedLanguage shown(language);
        const auto lang = language==i18n::Language::english ? "en" : "ja";
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
            named.view.onDetails();
            require(named.view.screen().detailsText.contains(a),"named history preserves chosen card name");
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
                fits(h.view,juce::String("PREFERENCE ")+lang+" "+phase+" "+juce::String(width));
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
            h.heard(3); h.choose(1); h.heard(3); h.choose(2);
            require(h.view.preferenceTrial().roundNumber()==3 && !selector->isVisible(),"answered selector buttons replaced by progress");
            require(progress->isVisible()==(width>=450),"small-size progress fallback does not overlap");
            if (auto* label=dynamic_cast<juce::Label*>(progress); label && label->isVisible())
                require(label->getFont().getHeight()>=12.0f,"progress text remains at least 12px");
            verifyCoverRoundTrip(true);
            save("round3"); h.reveal(); save("result");
            h.view.setPreferenceApprovedGains(h.status.generation,gain);
            h.view.onDetails(); save("details");
            auto* detailsEditor=dynamic_cast<juce::TextEditor*>(childById("live-blind-details-body"));
            require(detailsEditor && detailsEditor->isReadOnly(),"details scroll preview uses the read-only body");
            if (detailsEditor) {
                const auto originalText=detailsEditor->getText();
                detailsEditor->moveCaretToEnd();
                require(detailsEditor->getCaretPosition()==originalText.length()
                    && detailsEditor->getText()==originalText,"scrolling to details end preserves all text");
                auto* viewport = [&]() -> juce::Viewport* {
                    for (int i=0;i<detailsEditor->getNumChildComponents();++i)
                        if (auto* v=dynamic_cast<juce::Viewport*>(detailsEditor->getChildComponent(i))) return v;
                    return nullptr;
                }();
                require(viewport && viewport->getViewedComponent(),"details has a measurable text viewport");
                if (viewport && viewport->getViewedComponent()) {
                    const auto last=detailsEditor->getCaretRectangleForCharIndex(juce::jmax(0,originalText.length()-1));
                    const auto visible=detailsEditor->getLocalArea(viewport,viewport->getLocalBounds());
                    require(last.getY()>=visible.getY() && last.getBottom()<=visible.getBottom(),
                        "details final character is vertically visible after moving caret to end");
                    const int bottom=viewport->getViewedComponent()->getHeight();
                    require(viewport->getViewPositionY()+viewport->getViewHeight()>=bottom-2,
                        "details viewport reaches content bottom, or all text fits without scrolling");
                }
                // The outer viewport's final position now shows the frequency row. Retain
                // a separate actual text-tail image for the compact SHARP/gain evidence.
                if (!candidate && width==300) {
                    auto* outer=dynamic_cast<juce::Viewport*>(childById("live-blind-details-viewport"));
                    require(outer && outer->getViewedComponent(),"compact text tail has outer viewport");
                    if (outer && outer->getViewedComponent()) {
                        auto* content=outer->getViewedComponent();
                        const auto textBounds=content->getLocalArea(detailsEditor,detailsEditor->getLocalBounds());
                        outer->setViewPosition(0,juce::jmax(0,textBounds.getBottom()-outer->getViewHeight()));
                        const int sharpIndex=originalText.indexOf("SHARP:");
                        require(sharpIndex>=0 && originalText.contains("1.2")
                            && originalText.contains(juce::String::fromUTF8(u8"−2.3")),
                            "compact text tail includes SHARP and signed approved gains");
                        if (sharpIndex>=0) {
                            const auto first=content->getLocalArea(detailsEditor,
                                detailsEditor->getCaretRectangleForCharIndex(sharpIndex));
                            const auto last=content->getLocalArea(detailsEditor,
                                detailsEditor->getCaretRectangleForCharIndex(juce::jmax(0,originalText.length()-1)));
                            const auto visible=outer->getViewArea();
                            require(first.getY()>=visible.getY() && last.getBottom()<=visible.getBottom(),
                                "SHARP through final gain line is visible in compact outer viewport");
                            const auto viewInHost=h.view.getLocalArea(outer,outer->getLocalBounds());
                            require(h.view.getLocalBounds().contains(viewInHost),"text-tail viewport remains inside BLIND");
                        }
                        require(detailsEditor->getText()==originalText && detailsEditor->isReadOnly(),
                            "text-tail scroll changes neither data nor read-only state");
                        save("details-text-tail");
                    }
                }
                if (auto* outer=dynamic_cast<juce::Viewport*>(childById("live-blind-details-viewport")))
                    outer->setViewPosition(0,outer->getViewedComponent()->getHeight());
                save("details-end");
            }
            if (!candidate) require(h.view.screen().detailsText.contains("PSR")
                && h.view.screen().detailsText.contains(i18n::tr("Not shown because the measured time is too short")),
                "unmeasured details give explicit insufficient-time reason, not invented values");
            if (candidate) require(!h.view.screen().detailsText.contains("dB"),"A/B details contains no unsupported measured values");
            else {
                require(h.view.screen().detailsText.contains("1.2") && h.view.screen().detailsText.contains(juce::String::fromUTF8(u8"−2.3")),"validated approved gain displayed");
                const auto hasGain = [&] { return h.view.screen().detailsText.contains("1.2") && h.view.screen().detailsText.contains(juce::String::fromUTF8(u8"−2.3")); };
                auto changed=gain; ++changed.revision;
                h.view.setPreferenceApprovedGains(h.status.generation,changed);
                require(!hasGain(),"new revision cannot replace initial captured gain");
                changed=gain; ++changed.identity.ownerA;
                h.view.setPreferenceApprovedGains(h.status.generation,changed);
                require(!hasGain(),"new owner identity withdraws gain");
                h.view.setPreferenceApprovedGains(h.status.generation,gain);
                require(hasGain(),"same original tuple can revalidate");
                h.view.setPreferenceApprovedGains(h.status.generation+1,gain);
                require(!hasGain(),"foreign generation withdraws gain details");
                h.view.setPreferenceApprovedGains(h.status.generation,std::nullopt);
                require(!hasGain(),"missing receipt withdraws gain");
                h.status.stage=live_compare::BlindStage::idle; h.publish();
                ++h.status.generation; ++h.status.trial.epoch; h.status.trial.revealed=false;
                h.status.stage=live_compare::BlindStage::active; h.publish();
                changed=gain; changed.pre=1.0f; changed.post=1.0f; ++changed.revision;
                h.view.setPreferenceApprovedGains(h.status.generation,changed);
                h.heard(3); h.choose(1); h.reveal(); h.view.onDetails();
                require(!hasGain() && h.view.screen().detailsText.contains("0.0"),"END clears stored tuple so new trial captures new gain");
            }
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
    require(!file.exists(),"details never writes preference history");
    std::cout << "Blind PREFERENCE component: PASS (state, next-setting, partial reveal, images, EN/JA x 300/600/900)\n";
}
}
