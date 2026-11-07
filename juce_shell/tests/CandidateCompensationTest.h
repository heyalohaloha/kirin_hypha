#pragma once
#include "CandidateProductHost.h"
#include "CandidateChainHost.h"
#include "CandidateRecoveryTest.h"

namespace hypha::tests
{
template<class Require> void verifyCandidateBlindStart (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    for (const bool firstA : { false, true })
    {
        CandidateHost<Require> host (wall, require, firstA ? 2 : 1, 512);
        host.pair(); auto& post = host.post();
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "BLIND boundary fixture enters through the real candidate API");
        host.render (1100, true, false);
        const auto measured = post.measureLiveCompare();
        require (measured.ok() && post.applyLiveCompareMatch (planMatch (measured, 0.0), MatchChoice::basis),
                 "BLIND boundary uses the real settled MATCH");
        host.render (100, true, false);
        host.initialBlindSourceA = firstA;
        host.wholeOutput = true;
        require (post.beginLiveBlind() == StartResult::started, "BLIND boundary uses real preparation");
        for (int i = 0; i < 200 && host.blindStarts == 0; ++i)
        {
            require (!LiveTimingFixtureAccess::command (post).active(), "the admission callback still opens on named B");
            host.render (1, true, false);
            requireCandidateRawPost (host, require);
        }
        require (host.blindStarts == 1 && post.liveBlindStatus().stage == BlindStage::active
            && LiveTimingFixtureAccess::command (post).firstPre() == firstA,
                 "real service activates each anonymous assignment only after the checked B callback");
        host.render (4, true, false); // includes every sample of the first anonymous fade
        require (LiveTimingFixtureAccess::blindTrial (post).played == 1,
                 "the first source is heard only in callbacks after BLIND admission");
        require (post.selectLiveBlind (2), "the other anonymous source remains selectable");
        host.render (4, true, false);
        require (post.liveBlindStatus().trial.played == 3 && post.revealLiveBlind()
            && post.liveBlindStatus().trial.firstPre == firstA, "both assignments reveal the audio actually heard");
    }
}

// The host reports the hardware insert through its real adapter. Audio still traverses the
// real PRE1/PRE2/POST callbacks with independent nonlinear, echo and delay oracles on both sides.
template<class Require> void verifyCandidateHardwarePlacement (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    for (const int profile : { 0, 1, 2 }) // generic, REAPER and compensated AAX clocks
        for (const bool hardwareA : { false, true })
        {
            std::cout << "Candidate hardware: profile=" << profile << " insert=" << (hardwareA ? "A" : "B") << '\n' << std::flush;
            ValidationStorageSandbox isolated;
            CandidateChainHost api; api.hardwareA = hardwareA;
            CandidateHost<Require> host (wall, require, profile == 0 ? 1 : 2, profile == 2 ? 1024 : 512, profile == 2);
            host.delayA = hardwareA ? 4096 : 127; host.delayB = hardwareA ? 383 : 4096;
            api.delay = host.delayA + host.delayB;
            // Hardware cycles can report one position for several small callbacks. A-side
            // hardware puts both PRE2 and POST after that cycle; B-side hardware only POST.
            if (profile == 2)
            {
                host.clocks[2].reportFrames = 4 * host.block;
                if (hardwareA) host.clocks[1].reportFrames = 4 * host.block;
            }
            if (profile == 1)
            {
                for (auto& processor : host.instances)
                {
                    require (LiveTimingFixtureAccess::configureReaperClock (*processor), "hardware fixture configures the actual REAPER adapter");
                    processor->getVST3ClientExtensions()->setIHostApplication (&api);
                }
                host.processorTimers = true;
            }
            host.pair(); auto& post = host.post();
            enterCandidateThroughEditor (post, require);
            host.wholeOutput = true; // a coarse host can first admit A after the short helper's warm-up
            post.selectLiveComparePre (true); host.render (1100, true, true);
            require (host.replacementBlocks > 900 && post.liveCompareStatus().preAudible,
                     "either hardware placement actually substitutes aligned unity x and plays independent A(x)");
            if (profile == 2)
                require (LiveTimingFixtureAccess::chunkProven (post)
                    && (!hardwareA || LiveTimingFixtureAccess::chunkProven (host.pre2())),
                    "coarse hardware-cycle reports are actually interpolated on each downstream reader");
            require (post.liveCompareStatus().reaperRestriction == ReaperChainRestriction::none,
                     "hardware position alone never forbids comparison");
            host.wholeOutput = false;
            post.selectLiveComparePre (false); host.render (80, true, false);
            const auto measured = post.measureLiveCompare();
            require (measured.ok() && post.applyLiveCompareMatch (planMatch (measured, 0.0), MatchChoice::basis),
                     "both hardware placements MATCH only settled, verified A(x) and B(x)");
            host.render (100, true, false);
            // MATCH is settled and B is sounding. Audit the entire BLIND admission and all
            // later discontinuities; neither calibration length nor assignment selects a skip.
            host.outputOracle = {};
            host.receiverOracle.previouslyExact = true;
            for (int ch = 0; ch < host.channels; ++ch)
            {
                host.outputOracle.previous[static_cast<std::size_t> (ch)] = host.audio.getSample (ch, host.block - 1);
                host.receiverOracle.last[static_cast<std::size_t> (ch)] = host.history (ch, host.cursor - 1);
            }
            host.wholeOutput = true;
            require (post.beginLiveBlind() == StartResult::started, "hardware comparison enters the existing BLIND");
            host.render (1000, true, false);
            require (post.liveBlindStatus().stage == BlindStage::active, "hardware BLIND obtains a real anonymous assignment");
            const auto epoch = post.liveBlindStatus().trial.epoch;
            for (const int source : { 1, 2 })
            { require (post.selectLiveBlind (source), "both anonymous sources can be selected"); host.render (40, true, false); }
            require (post.liveBlindStatus().trial.played == 3 && post.revealLiveBlind(), "both independently verified sources allow REVEAL");
            for (auto& clock : host.clocks) clock.offset -= 96000;
            host.render (160, true, false);
            host.delayA += 1024; host.delayB += 1024; api.delay = host.delayA + host.delayB;
            host.render (200, true, false);
            require (post.liveCompareStatus().verdict == Verdict::accepted && post.liveBlindStatus().trial.epoch == epoch,
                     "seek and changed hardware delay automatically resume the same trial with correct audio");
            if (profile != 0)
            {
                for (auto& clock : host.clocks) clock.loopLength = 48000;
                host.render (300, true, false);
                require (post.liveCompareStatus().verdict == Verdict::accepted && post.liveBlindStatus().trial.epoch == epoch,
                         "certified loops support hardware on either side without changing the assignment");
            }
            // A loop can leave B's echo carrying an earlier unverified input. Keep the
            // full actual-history oracle through both recovered anonymous selections.
            for (const int source : { 1, 2 })
            { require (post.selectLiveBlind (source), "recovered hardware sources stay selectable"); host.render (80, true, false); }
            host.wholeOutput = false;
            post.finishLiveCompare(); host.render (12, true, false, false); host.render (700, false, false);
            require (!post.candidateMeasurementIsolated(), "either hardware placement returns to the original serial chain");
            if (profile == 1)
                for (auto& processor : host.instances) processor->getVST3ClientExtensions()->setIHostApplication (nullptr);
        }
}

template<class Require> void verifyCandidateCompensation (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    for (const bool blind : { false, true })
    {
        CandidateHost<Require> host (wall, require, 2, 512, true);
        host.pair(); auto& post = host.post();
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "compensated candidate starts through the real entry");
        post.selectLiveComparePre (true); host.render (1000, true, true);
        if (blind)
        {
            require (post.beginLiveBlind() == StartResult::started, "compensated candidate enters BLIND");
            host.render (1000, true, false, false);
            host.render (40, true, false);
            require (post.liveBlindStatus().stage == BlindStage::active, "BLIND has an actual anonymous assignment");
        }
        const auto assignment = post.liveBlindStatus().trial.firstPre;
        const auto selected = post.liveBlindStatus().trial.selected;
        for (auto& processor : host.instances) processor->kirinHostDelayCompensationStateChanged (false);
        host.render (4, true, false, false); // transition may use its last output value, never old PCM
        const auto count = host.replacementBlocks;
        host.render (400, false, false, false); // checks every PRE2 sample against actual serial input
        require (host.replacementBlocks == count && post.candidateCompareActive(),
                 "compensation off leaves PRE2 passing through while preserving the chosen comparison");
        require (blind ? post.liveBlindStatus().stage == BlindStage::active
            && post.liveBlindStatus().trial.firstPre == assignment && post.liveBlindStatus().trial.selected == selected
            : post.liveCompareStatus().preSelected, "compensation off retains selection and anonymous assignment");
        const auto command = LiveTimingFixtureAccess::command (post);
        const auto approvedGain = post.liveCompareStatus().gain;
        for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        {
            const i18n::ScopedLanguage scoped (language);
            auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
            editor->setVisible (true);
            for (const auto size : observatory::sizePresets)
            {
                editor->setSize (size.width, size.height); candidateTick (*editor);
                const auto namedText = text_style::shownText ("Compensation off: enable it (POST)");
                if (language == i18n::Language::japanese)
                    require (namedText == juce::String::fromUTF8 (u8"遅延補正（PDC）を有効にしてください（POST）"),
                             "Japanese guidance explicitly enables delay compensation");
                if (blind)
                {
                    auto* screen = candidateControl (*editor, "live-blind-screen");
                    auto* label = screen ? dynamic_cast<juce::Label*> (candidateControl (*screen, "live-blind-text-5")) : nullptr;
                    require (screen && screen->isVisible() && label
                        && text_style::shownText (label->getText()) == text_style::shownText ("Enable DAW delay compensation"),
                        "BLIND shows the compensation remedy at every size and language");
                    const auto view = live_blind_ui::screenFor (post.liveBlindStatus(), true, 1.0f);
                    require (view.sourceOne == "SOURCE 1" && view.sourceTwo == "SOURCE 2", "compensation guidance preserves anonymity");
                }
                else
                {
                    const auto warning = dynamic_cast<KirinHyphaEditor&> (*editor).*candidateEditorTick (CandidateEditorWarning {});
                    require (text_style::shownText (warning) == namedText, "named warning shows the compensation remedy");
                    auto* button = dynamic_cast<juce::Button*> (candidateControl (*editor, "observatory-menu"));
                    require (button && button->onClick && juce::MessageManager::callAsync ([button] { button->onClick(); }),
                             "open actual comparison MENU during compensation OFF");
                    require (afterQueuedMessages ([&]
                    {
                        auto& modal = *juce::ModalComponentManager::getInstance();
                        require (modal.getNumModalComponents() == 1, "comparison MENU opens");
                        auto* menu = modal.getModalComponent (0);
                        const auto text = candidateMenuText (*menu);
                        // The look and feel translates as it paints, so accessible titles keep the source text.
                        require (text.contains ("Compensation off: enable it (POST)") && !text.contains ("A/B: timing unavailable; check PRE1 and PRE2"),
                                 "MENU keeps the compensation remedy instead of generic guidance");
                        menu->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
                    }), "inspect and close comparison MENU");
                }
                const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_CANDIDATE_PREVIEW", {});
                if (preview.isNotEmpty())
                {
                    auto stream = juce::File (preview).getChildFile ("a12-" + juce::String (blind ? "blind" : "named")
                        + "-compensation-" + juce::String (int (language)) + "-" + juce::String (size.width) + ".png").createOutputStream();
                    require (stream && juce::PNGImageFormat().writeImageToStream (
                        editor->createComponentSnapshot (editor->getLocalBounds()), *stream), "compensation preview renders");
                }
            }
            post.editorBeingDeleted (editor.get());
        }
        const auto heldCommand = LiveTimingFixtureAccess::command (post);
        require (post.liveCompareStatus().gain == approvedGain && (!blind || (command.epoch() == heldCommand.epoch()
            && command.stimulus() == heldCommand.stimulus() && command.firstPre() == heldCommand.firstPre())),
            "compensation guidance preserves gain and the anonymous trial");
        host.delayA += 1024;
        for (auto& processor : host.instances) processor->kirinHostDelayCompensationStateChanged (true);
        host.render (1, true, false, false);
        require (host.replacementBlocks == count, "reenabling compensation cannot reuse the previous K");
        host.render (300, true, !blind);
        require (host.replacementBlocks > count + 200 && post.liveCompareStatus().verdict == Verdict::accepted,
                 "new compensated positions automatically resume the same comparison at the correct delay");
        // Even OFF -> ON entirely between callbacks must discard the old correspondence.
        const auto before = host.replacementBlocks;
        host.pre2().kirinHostDelayCompensationStateChanged (false);
        host.pre2().kirinHostDelayCompensationStateChanged (true);
        host.render (1, true, false, false);
        require (host.replacementBlocks == before, "a compensation boundary between callbacks still revokes PRE2 timing");
        host.render (100, true, !blind);
        require (host.replacementBlocks > before + 60, "fresh proof resumes after a between-callback toggle");
        if (!blind) continue;
        host.pre2().getBypassParameter()->setValue (1.0f);
        host.render (1, true, false, false); // the short fade out of x
        host.render (399, false, false, false);
        require (post.liveBlindStatus().stage == BlindStage::active && post.liveBlindStatus().prolongedWait,
                 "bypassed PRE2 keeps BLIND active and eventually offers guidance");
        for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        {
            const i18n::ScopedLanguage scoped (language);
            auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
            editor->setVisible (true);
            for (const auto size : observatory::sizePresets)
            {
                editor->setSize (size.width, size.height); candidateTick (*editor);
                auto* screen = candidateControl (*editor, "live-blind-screen");
                require (screen && screen->isVisible(), "actual BLIND recovery screen remains open");
                auto* recovery = dynamic_cast<juce::Label*> (candidateControl (*screen, "live-blind-text-5"));
                require (recovery && text_style::shownText (recovery->getText()) == text_style::shownText ("A/B: enable PRE2; bypass is on"),
                         "actual BLIND label names PRE2 bypass in both languages at every size");
                const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_CANDIDATE_PREVIEW", {});
                if (preview.isNotEmpty() && (size.width == 300 || size.width == 900))
                {
                    auto stream = juce::File (preview).getChildFile ("blind-recovery-" + juce::String (int (language))
                        + "-" + juce::String (size.width) + ".png").createOutputStream();
                    require (stream && juce::PNGImageFormat().writeImageToStream (
                        editor->createComponentSnapshot (editor->getLocalBounds()), *stream), "actual recovery preview renders");
                }
                const auto view = live_blind_ui::screenFor (post.liveBlindStatus(), true, 1.0f);
                require (view.sourceOne == "SOURCE 1" && view.sourceTwo == "SOURCE 2",
                         "PRE2 recovery guidance reveals no anonymous assignment");
            }
            post.editorBeingDeleted (editor.get());
        }
        host.pre2().getBypassParameter()->setValue (0.0f); host.render (300, true, false);
        require (post.liveBlindStatus().stage == BlindStage::active
            && post.liveBlindStatus().trial.firstPre == assignment && post.liveBlindStatus().trial.selected == selected
            && !post.liveBlindStatus().prolongedWait, "BLIND resumes the same assignment after PRE2 is enabled");
    }
}
}
