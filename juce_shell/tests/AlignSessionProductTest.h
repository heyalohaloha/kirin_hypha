#pragma once
#include "AlignContextProductTest.h"
#include "EditorProductChecks.h"
#include "ReferenceValidationStorageSandbox.h"
#include <chrono>
#include <thread>

namespace hypha::tests::editor_product
{
struct RefreshBlind { using Type = void (KirinHyphaEditor::*)(); friend Type privateMember (RefreshBlind); };
template struct PrivateAccess<RefreshBlind, &KirinHyphaEditor::refreshLiveBlind>;
}
namespace aax_analysis_product
{
inline void selectPreparedPair (ChunkedPost& chain, const char* why)
{
    auto due = std::chrono::steady_clock::now();
    const auto period = std::chrono::nanoseconds (ChunkedPost::chunkFrames * 1'000'000'000LL / 48000);
    for (int i = 0; i < 200; ++i)
    {
        chain.chunk();
        if (chain.post->setPairCandidate (chain.pre->instanceId(), {})) return;
        due += period;
        if (std::chrono::steady_clock::now() < due) std::this_thread::sleep_until (due);
    }
    require (false, why);
}

inline void restartingConfirmsNewContent (int channels)
{
    using namespace hypha::live_compare;
    for (bool blind : { false, true })
    {
        ChunkedPost chain (channels);
        chain.requireContentPosition = true;
        auto& post = *chain.post;
        selectPreparedPair (chain, "select restart PRE");
        require (post.startLiveCompare() == StartResult::started, "first LISTEN starts");
        post.selectLiveComparePre (true);
        chain.until (90, [&] { return post.liveCompareStatus().preAudible; }, "first session confirms its content");
        const auto old = post.measureLiveCompareOffset();
        require (offsetCanVote (old), "first session has strong content evidence");
        post.finishLiveCompare();
        chain.until (30, [&] { const auto s = post.liveCompareStatus(); return !s.active && !s.finishing; }, "END finishes");
        chain.contentK += 1024; // only the audio delay changes; pair, format, owner and clock K stay the same
        require ((blind ? post.beginLiveBlind() : post.startLiveCompare()) == StartResult::started, "same pair restarts");
        if (!blind) post.selectLiveComparePre (true);
        for (int i = 0; i < 40; ++i) chain.chunk (false); // fresh clocks and PCM, no new content confirmation
        require (!post.liveCompareStatus().preAudible && post.liveCompareAlignPending(),
                 "a new session cannot use its predecessor's confirmation");
        LiveTimingFixtureAccess::publishMeasuredAlign (post, old);
        chain.chunk (false);
        require (!post.liveCompareStatus().preAudible, "an old in-flight estimate cannot authorize the restarted session");
        const auto current = post.measureLiveCompareOffset();
        require (offsetCanVote (current) && current.clockK == old.clockK, "content changed while clock K stayed fixed");
        chain.until (120, [&] {
            if (blind) post.serviceLiveBlind();
            return !post.liveCompareAlignPending();
        }, "the new content automatically completes confirmation");
        if (blind)
        {
            chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; },
                         "the restarted trial separately completes its MATCH preparation");
            LiveTimingFixtureAccess::assignBlind (post, true);
            require (post.selectLiveBlind (1), "select the restarted trial's PRE");
        }
        chain.until (30, [&] { return post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 2048; },
                     "only the newly confirmed position sounds");
        chain.clean ("END and restart never sound an old content position");
    }
}

inline void publicationKeepsConfirmedAudio (int channels)
{
    using namespace hypha::live_compare;
    for (bool blind : { false, true })
    for (bool zero : { false, true })
    {
        ChunkedPost chain (channels);
        chain.requireContentPosition = true;
        if (zero) chain.setDelay (512);
        auto& post = *chain.post;
        selectPreparedPair (chain, "select publication PRE");
        require ((blind ? post.beginLiveBlind() : post.startLiveCompare()) == StartResult::started, "publication session starts");
        if (blind)
        {
            chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; },
                         "publication BLIND prepares real gains");
            LiveTimingFixtureAccess::assignBlind (post, true);
            require (post.selectLiveBlind (1), "select publication PRE anonymously");
        }
        else post.selectLiveComparePre (true);
        chain.until (90, [&] { return post.liveCompareStatus().preAudible && !post.liveCompareAlignPending(); },
                     "the published address is already confirmed and sounding");
        chain.run (4);
        const auto estimate = post.measureLiveCompareOffset();
        require (offsetCanVote (estimate), "publication has fresh strong content evidence");
        bool contended = false;
        publicationInterleave = [&] (std::int64_t) {
            contended = true;
            chain.chunk (false);
            require (post.liveCompareStatus().preAudible && !post.liveCompareStatus().preWaiting,
                     "a same-generation publication cannot replace confirmed PRE with POST");
        };
        duringAlignPublicationForTest = [] (std::int64_t value) { publicationInterleave (value); };
        // Zero is periodically republished by the service. For a nonzero correction the
        // fixture uses that same real publication slot, with the current measured context.
        LiveTimingFixtureAccess::publishMeasuredAlign (post, estimate);
        duringAlignPublicationForTest = nullptr;
        publicationInterleave = {};
        require (contended, "an actual callback crossed the incomplete publication");
        chain.clean ("every sample remains on the same confirmed source during publication");
    }
}

inline void waitingBlindCanDisableAlignment (int channels)
{
    using namespace hypha::live_compare;
    using hypha::tests::editor_product::RefreshBlind;
    // A confident late PRE is a delay to fix: the trial keeps POST and offers ALIGN OFF.
    for (bool late : { true })
    {
        hypha::blind_ui::Screen waitingScreens[2];
        for (bool firstPre : { false, true })
        {
            ChunkedPost chain (channels);
            chain.unrelatedPost = !late;
            auto& post = *chain.post;
            selectPreparedPair (chain, "select timing-recovery PRE");
            require (post.beginLiveBlind() == StartResult::started, "timing-recovery BLIND starts with default ON");
            chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; },
                         "the same section prepares BLIND");
            LiveTimingFixtureAccess::assignBlind (post, firstPre);
            require (post.selectLiveBlind (1), "select the first source while alignment waits");
            if (late) chain.setDelay (1024); // change the clock mapping before either source earns a receipt
            chain.run (190);
            const auto expected = late ? RecoveryReason::preLate : RecoveryReason::contentAmbiguous;
            require (post.liveBlindStatus().observation == expected, "the real input reaches the requested timing failure");
            require (post.liveCompareAlignPending() && post.liveBlindStatus().trial.played == 0 && !post.revealLiveBlind(),
                     "unconfirmed timing earns no anonymous receipt before the explicit OFF operation");
            const auto before = post.liveBlindStatus();
            waitingScreens[firstPre ? 1 : 0] = hypha::live_blind_ui::screenFor (before, true, post.liveCompareStatus().postActual);
            const auto gain = post.liveCompareStatus().gain;
            auto editor = std::unique_ptr<KirinHyphaEditor> (dynamic_cast<KirinHyphaEditor*> (post.createEditorIfNeeded()));
            require (editor != nullptr, "recovery uses the shipping editor");
            editor->setVisible (true);
            (editor.get()->*privateMember (RefreshBlind {}))();
            auto* button = dynamic_cast<juce::Button*> (hypha::tests::editor_product::find (*editor, "live-blind-align-off"));
            auto* end = hypha::tests::editor_product::find (*editor, "live-blind-end");
            require (button != nullptr && end != nullptr, "the waiting screen contains the explicit OFF operation and END");
            for (const auto language : { hypha::i18n::Language::english, hypha::i18n::Language::japanese })
            {
                hypha::i18n::ScopedLanguage localized (language);
                for (const auto width : { 300, 375, 450, 600, 900 })
                {
                    editor->setSize (width, width * 2 / 3);
                    (editor.get()->*privateMember (RefreshBlind {}))();
                    bool visible = true;
                    for (auto* ancestor = static_cast<juce::Component*> (button); ancestor != nullptr; ancestor = ancestor->getParentComponent())
                        visible = visible && ancestor->isVisible();
                    if (!visible || !button->isEnabled() || button->getBounds().intersects (end->getBounds()))
                    {
                        const auto* screen = dynamic_cast<hypha::live_blind_ui::Component*> (button->getParentComponent());
                        std::cerr << "OFF reachability: width " << width << " language " << static_cast<int> (language)
                                  << " visible " << button->isVisible() << " showing " << button->isShowing()
                                  << " enabled " << button->isEnabled() << " bounds " << button->getBounds().toString()
                                  << " END " << end->getBounds().toString();
                        if (screen) std::cerr << " shown " << screen->screen().alignOffShown
                            << " wait " << screen->state().prolongedWait << " observation " << static_cast<int> (screen->state().observation);
                        std::cerr << '\n';
                    }
                    require (visible && button->isEnabled() && !button->getBounds().intersects (end->getBounds()),
                             "OFF is reachable without moving END at every size and language");
                    require (button->getParentComponent()->getComponentAt (button->getBounds().getCentre()) == button,
                             "the visible OFF button receives its own click");
                }
            }
            button->onClick(); // actual UI callback changes the processor's existing saved preference
            require (!post.liveCompareAutoAlign(), "the waiting-screen operation explicitly disables automatic alignment");
            chain.run (12);
            const auto after = post.liveBlindStatus();
            require (after.trial.epoch == before.trial.epoch && after.trial.firstPre == before.trial.firstPre
                     && after.trial.selected == before.trial.selected && post.liveCompareStatus().gain == gain,
                     "OFF preserves this trial, assignment, selected source and approved gain");
            require (post.selectLiveBlind (2), "the second source stays selectable on the same material");
            chain.run (12);
            require (post.liveBlindStatus().trial.played == 3 && post.revealLiveBlind(),
                     "explicit OFF allows both actual sources and REVEAL without changing material or restarting");
            require (post.liveBlindStatus().trial.firstPre == firstPre, "REVEAL confirms that OFF preserved the true anonymous assignment");
            chain.clean ("explicit OFF still uses only proven clock addresses");
            post.editorBeingDeleted (editor.get());
            editor.reset();
        }
        require (waitingScreens[0] == waitingScreens[1], "timing recovery reveals no anonymous assignment");
    }
}

// Unconfirmable content never holds a trial on POST past the bounded wait: both sources sound at
// their clock positions, the trial earns its receipts and REVEAL, alignment stays ON, and no wait
// screen or ALIGN OFF tells the anonymous sources apart.
inline void waitingBlindResumesAtItsClock (int channels)
{
    using namespace hypha::live_compare;
    for (bool late : { false })
    {
        hypha::blind_ui::Screen screens[2];
        for (bool firstPre : { false, true })
        {
            ChunkedPost chain (channels);
            chain.unrelatedPost = !late;
            auto& post = *chain.post;
            selectPreparedPair (chain, "select timing-recovery PRE");
            require (post.beginLiveBlind() == StartResult::started, "timing-recovery BLIND starts with default ON");
            chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; },
                         "the same section prepares BLIND");
            LiveTimingFixtureAccess::assignBlind (post, firstPre);
            require (post.selectLiveBlind (1), "select the first source");
            if (late) chain.setDelay (1024); // change the clock mapping before either source earns a receipt
            chain.run (190);
            const auto waited = post.liveBlindStatus();
            require (!waited.prolongedWait && waited.observation != RecoveryReason::preLate
                     && waited.observation != RecoveryReason::contentAmbiguous && !post.liveCompareAlignPending()
                     && post.liveCompareAlignFrames() == 0 && post.liveCompareAutoAlign(),
                     "the bounded wait ends without a notice and alignment stays ON");
            auto& screen = screens[firstPre ? 1 : 0];
            screen = hypha::live_blind_ui::screenFor (waited, true, post.liveCompareStatus().postActual);
            require (!screen.guidanceShown && !screen.alignOffShown && *hypha::live_compare_ui::blindRecovery (waited, false).instruction == 0,
                     "no wait screen, remedy or ALIGN OFF after the bounded wait");
            require (post.liveCompareStatus().preAudible == firstPre && waited.trial.played == 1,
                     "the selected source sounds at its clock position and earns its receipt");
            require (post.selectLiveBlind (2), "the second source stays selectable on the same material");
            chain.run (12);
            require (post.liveBlindStatus().trial.played == 3 && post.revealLiveBlind(),
                     "both actual sources and REVEAL without OFF, another section or a restart");
            require (post.liveBlindStatus().trial.firstPre == firstPre, "REVEAL confirms the true anonymous assignment");
            chain.clean ("the bounded wait still uses only proven clock addresses");
        }
        require (screens[0] == screens[1], "timing recovery reveals no anonymous assignment");
    }
}

inline void heardSourcesCanRevealDuringRecheck (int channels)
{
    using namespace hypha::live_compare;
    using hypha::tests::editor_product::RefreshBlind;
    for (bool late : { false, true })
    for (bool firstPre : { false, true })
    {
        ChunkedPost chain (channels);
        chain.requireContentPosition = true;
        auto& post = *chain.post;
        selectPreparedPair (chain, "select heard-source PRE");
        require (post.beginLiveBlind() == StartResult::started, "heard-source BLIND starts");
        chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; },
                     "heard-source BLIND prepares actual gains");
        LiveTimingFixtureAccess::assignBlind (post, firstPre);
        for (int source : { 1, 2 })
        {
            require (post.selectLiveBlind (source), "select each actual anonymous source");
            chain.until (90, [&] { return (post.liveBlindStatus().trial.played & (1 << (source - 1))) != 0; },
                         "each source earns its own audio receipt");
        }
        const auto heard = post.liveBlindStatus();
        const auto gain = post.liveCompareStatus().gain;
        require (heard.trial.played == 3 && !heard.trial.revealed, "REVEAL is earned before timing changes");
        chain.unrelatedPost = !late;
        chain.setDelay (late ? 1024 : 512);
        chain.until (90, [&] { return post.liveCompareAlignPending(); }, "changed clocks start a real content recheck");
        require (!post.liveBlindStatus().prolongedWait, "the recheck is short");
        auto editor = std::unique_ptr<KirinHyphaEditor> (dynamic_cast<KirinHyphaEditor*> (post.createEditorIfNeeded()));
        require (editor != nullptr, "earned REVEAL uses the shipping editor");
        editor->setVisible (true);
        (editor.get()->*privateMember (RefreshBlind {}))();
        auto* reveal = dynamic_cast<juce::Button*> (hypha::tests::editor_product::find (*editor, "live-blind-reveal"));
        auto* end = hypha::tests::editor_product::find (*editor, "live-blind-end");
        require (reveal != nullptr && end != nullptr, "earned REVEAL and END exist");
        for (const auto language : { hypha::i18n::Language::english, hypha::i18n::Language::japanese })
        {
            hypha::i18n::ScopedLanguage localized (language);
            for (const auto width : { 300, 375, 450, 600, 900 })
            {
                editor->setSize (width, width * 2 / 3);
                (editor.get()->*privateMember (RefreshBlind {}))();
                require (reveal->isVisible() && reveal->isEnabled() && !reveal->getBounds().intersects (end->getBounds()),
                         "earned REVEAL stays reachable during a recheck in each language and size");
                require (reveal->getParentComponent()->getComponentAt (reveal->getBounds().getCentre()) == reveal,
                         "earned REVEAL receives its click during the recheck");
            }
        }
        reveal->onClick();
        const auto revealed = post.liveBlindStatus();
        require (revealed.trial.revealed && revealed.trial.played == 3 && revealed.trial.epoch == heard.trial.epoch
                 && revealed.trial.firstPre == firstPre && revealed.trial.selected == heard.trial.selected
                 && post.liveCompareStatus().gain == gain && post.liveCompareAutoAlign(),
                 "REVEAL keeps the same assignment, choice, gains and automatic alignment setting");
        require (post.liveCompareAlignPending() && !post.liveCompareStatus().preAudible,
                 "REVEAL grants no unconfirmed PRE output");
        if (late) chain.setDelay (512);
        chain.until (90, [&] { return !post.liveCompareAlignPending(); }, "revealed comparison still recovers automatically");
        chain.unrelatedPost = false;
        chain.clean ("earned REVEAL never authorizes an unconfirmed address");
        post.editorBeingDeleted (editor.get());
        editor.reset();
    }
}

// Pro Tools: PRE -> EQ -> a lookahead limiter (threshold -4 dB, ceiling -0.3 dBFS) -> POST, its
// delay compensated. Music leaves most of the spectrum nearly empty and the chain's noise fills
// it, so the content check confirms no position. LISTEN and BLIND wait at most one second of
// proven playback, then PRE sounds at its clock position without a notice, as with alignment
// off. The same music without the chain, PRE early across an I/O insert, is still aligned.
inline void unconfirmableChainPlaysPreAtItsClock (int channels)
{
    using namespace hypha::live_compare;
    for (bool blind : { false, true })
    for (bool limited : { true, false })
    {
        ChunkedPost chain (channels);
        chain.chord = true;
        chain.limited = limited;
        chain.requireContentPosition = true;
        if (limited) chain.setDelay (512); // the host compensates the limiter: K is the content position
        auto& post = *chain.post;
        selectPreparedPair (chain, "select limiter-chain PRE");
        require ((blind ? post.beginLiveBlind() : post.startLiveCompare()) == StartResult::started, "limiter chain starts");
        if (blind)
        {
            chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; },
                         "limiter-chain BLIND prepares from actual PCM");
            LiveTimingFixtureAccess::assignBlind (post, true);
            require (post.selectLiveBlind (1), "select the limiter-chain PRE anonymously");
        }
        else post.selectLiveComparePre (true);
        // The POST was just inserted: its first content check starts with this comparison.
        chain.until (60, [&] { return post.liveCompareStatus().preAudible; }, "PRE sounds within the bounded wait");
        const auto estimate = post.measureLiveCompareOffset();
        if (limited)
            require (estimate.windowReady && !offsetCanVote (estimate) && post.liveCompareAlignFrames() == 0,
                     "the limiter chain confirms no position and PRE plays at its clock position");
        else
            require (offsetCanVote (estimate) && post.liveCompareAlignFrames() == 1024,
                     "the same music without the chain still aligns an early PRE");
        chain.run (190);
        const auto status = post.liveCompareStatus();
        require (status.preAudible && !status.prolongedWait && !post.liveCompareAlignPending()
                 && status.observation != RecoveryReason::contentAmbiguous, "no wait and no notice after the bounded wait");
        if (blind)
        {
            require (*hypha::live_compare_ui::blindRecovery (post.liveBlindStatus(), false).instruction == 0,
                     "BLIND shows no timing notice");
            for (int source : { 2, 1 }) { require (post.selectLiveBlind (source), "switch the anonymous sources"); chain.run (12); }
            require (post.liveCompareStatus().preAudible && post.liveBlindStatus().trial.played == 3 && post.revealLiveBlind(),
                     "both sources sound and REVEAL is earned");
            require (!post.liveBlindStatus().trial.usedPostFallback, "RESULT names no POST substitute for PRE");
        }
        else require (*hypha::live_compare_ui::namedPresentation (status).instruction == 0, "LISTEN shows no timing notice");
        chain.clean ("every PRE sample is at its clock position or its confirmed alignment");
    }
}

// Pro Tools trace: a bass-heavy section through EQ3 and Maxim, BLIND begun just after POST was
// inserted, PRE chosen at once. Two thirds of the windows are narrow; after the chain they are no
// scaled copy, yet they only fail to judge: they raise no doubt and break no consecutive votes,
// and the broadband windows between them confirm the unchanged address while PRE is chosen.
inline void narrowBassBlindPlaysPre (int channels)
{
    using namespace hypha::live_compare;
    ChunkedPost chain (channels);
    chain.bass = chain.limited = chain.requireContentPosition = true;
    chain.setDelay (512); // the host compensates the plug-ins: K is the content position
    auto& post = *chain.post;
    selectPreparedPair (chain, "select bass-chain PRE");
    require (post.beginLiveBlind() == StartResult::started, "bass-chain BLIND starts with alignment ON");
    int narrow = 0, votes = 0;
    chain.until (400, [&] {
        post.serviceLiveBlind();
        const auto e = post.measureLiveCompareOffset();
        narrow += e.windowReady && e.narrow; votes += offsetCanVote (e);
        require (post.liveCompareStatus().observation != RecoveryReason::contentAmbiguous, "narrow windows raise no doubt");
        return post.liveBlindStatus().stage == BlindStage::active; }, "bass-chain BLIND prepares");
    LiveTimingFixtureAccess::assignBlind (post, true);
    require (post.selectLiveBlind (1), "choose PRE at once");
    chain.until (60, [&] { return post.liveCompareStatus().preAudible; }, "PRE sounds right after the choice");
    for (int round = 0; round < 2; ++round)
    {
        if (round == 1)
        {
            chain.setDelay (1024); chain.contentK = 1024; // a new clock mapping, the same address
            chain.until (90, [&] { return post.liveCompareAlignPending(); }, "the new mapping is rechecked under chosen PRE");
        }
        chain.until (200, [&] {
            const auto e = post.measureLiveCompareOffset();
            narrow += e.windowReady && e.narrow; votes += offsetCanVote (e);
            require (post.liveCompareStatus().observation != RecoveryReason::contentAmbiguous, "narrow windows raise no doubt");
            return post.liveCompareStatus().preAudible && post.liveContentTimingObservation().settled; },
            "the broadband windows confirm the unchanged address while PRE is chosen");
        require (post.liveContentTimingObservation().lagFrames == 0 && post.liveCompareAlignFrames() == 0
                 && !post.liveCompareAlignPending() && !post.liveBlindStatus().prolongedWait,
                 "content confirms PRE at its clock position without a wait or notice");
        // RESULT tells the truth: nothing replaced PRE at the start; the recheck itself did.
        require (post.liveBlindStatus().trial.usedPostFallback == (round == 1), "RESULT names POST only where it played");
    }
    require (narrow > 0 && votes > 0, "the section really mixes narrow and broadband windows");
    for (int source : { 2, 1 }) { require (post.selectLiveBlind (source), "switch the anonymous sources"); chain.run (12); }
    require (post.liveBlindStatus().trial.played == 3 && post.revealLiveBlind(), "both sources sound and REVEAL is earned");
    chain.clean ("every PRE sample is the content position");
}

inline void verifyAlignSessions (int channels)
{
    ReferenceValidationStorageSandbox sandbox;
    hypha::tests::editor_product::ScopedChainTimingStorage preferences (sandbox.directory());
    restartingConfirmsNewContent (channels);
    publicationKeepsConfirmedAudio (channels);
    waitingBlindCanDisableAlignment (channels);
    waitingBlindResumesAtItsClock (channels);
    heardSourcesCanRevealDuringRecheck (channels);
    unconfirmableChainPlaysPreAtItsClock (channels);
}
// Exact periodic PCM has several possible addresses. Compare the checked waveform, and
// reject the first changed block at that address before it can sound. No timeout grants PRE.
inline void repeatedAudioKeepsComparisonMoving (int channels)
{
    using namespace hypha::live_compare;
    for (bool blind : { false, true })
    {
        ChunkedPost chain (channels);
        chain.tonal = true;
        chain.requireContentPosition = true;
        auto& post = *chain.post;
        selectPreparedPair (chain, "select repeated-audio PRE");
        require ((blind ? post.beginLiveBlind() : post.startLiveCompare()) == StartResult::started,
                 "repeated audio starts through the real comparison entry");
        if (blind)
        {
            chain.until (400, [&] { return post.liveBlindStatus().stage == BlindStage::active; },
                         "periodic input prepares BLIND without OFF or a section change");
            LiveTimingFixtureAccess::assignBlind (post, true);
            require (post.selectLiveBlind (1), "select the periodic PRE source");
        }
        else post.selectLiveComparePre (true);
        chain.until (120, [&] { return post.liveCompareStatus().preAudible && !post.liveCompareAlignPending(); },
                     "periodic input permits checked comparison without an indefinite wait");
        require (post.liveCompareAutoAlign() && post.liveContentTimingObservation().verifySamples,
                 "periodic comparison keeps ON and requests a check of every block");
        chain.clean ("periodic output matches the independently known input position sample for sample");
        if (blind)
        {
            for (int source : { 1, 2 }) { require (post.selectLiveBlind (source), "select both periodic sources"); chain.run (12); }
            const auto trial = post.liveBlindStatus();
            require (trial.trial.played == 3, "both periodic sources earn real audio receipts");
            auto opposite = trial; opposite.trial.firstPre = !opposite.trial.firstPre;
            const auto a = hypha::live_blind_ui::screenFor (trial, true, post.liveCompareStatus().postActual, true);
            const auto b = hypha::live_blind_ui::screenFor (opposite, true, post.liveCompareStatus().postActual, true);
            require (a == b && a.detail == "Repeated audio: checked per block", "the short notice never identifies an anonymous source");
            require (post.selectLiveBlind (1), "keep PRE selected before the pattern changes");
            chain.run (12);
        }
        const auto held = chain.withheld;
        chain.tonal = false;
        chain.chunk (false); // no non-RT re-estimate: the renderer itself must catch this block
        require (!post.liveCompareStatus().preAudible && chain.withheld > held,
                 "the first nonmatching block is exact POST before a new estimate exists");
        chain.until (120, [&] { return post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 1024; },
                     "fresh unique PCM resumes the retained selection automatically");
        chain.clean ("no unchecked sample escapes through a fade or a BLIND source");
    }
}

}
