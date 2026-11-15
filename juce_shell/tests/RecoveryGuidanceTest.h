#pragma once
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaMatchChoicePopup.h"
namespace hypha::tests
{
template <class Require> void verifyMatchChoicePopup (Require require)
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const int width : { 300, 375, 450, 600, 750, 900 })
        for (const bool candidate : { false, true })
        {
            juce::Component parent;
            parent.setSize (width, width * 2 / 3);
            juce::TextButton end, listen, pre;
            for (auto* button : { &end, &listen, &pre }) parent.addAndMakeVisible (*button);
            end.setBounds (width - 70, parent.getHeight() - 30, 60, 26);
            listen.setBounds (width - 140, parent.getHeight() - 30, 60, 26);
            pre.setBounds (width - 210, parent.getHeight() - 30, 60, 26);
            const auto heading = juce::String (candidate ? "A needs " : "PRE needs ") + "+24.00 dB; TP ceiling allows +12.00 dB";
            const auto lower = juce::String (candidate ? "Lower B by " : "Lower POST by ") + "12.00 dB"
                + (candidate ? "; A stays at its level" : "; PRE stays at its level");
            const auto limit = juce::String (candidate ? "Raise A by " : "Raise PRE by ") + "+12.00 dB only (TP LIMIT)";
            if (language == i18n::Language::japanese)
                require (i18n::hasTranslation (heading) && i18n::hasTranslation (lower) && i18n::hasTranslation (limit),
                         "MATCH choices are translated");
            live_compare_ui::MatchChoicePopup popup;
            int chosen = 0, presses = 0;
            const auto open = [&]
            {
                popup.open (parent, listen.getBounds(), heading, lower, limit,
                    presentation::forEditor (width, width * 2 / 3), [&chosen] (int value) { chosen = value; });
            };
            for (auto* button : { &end, &listen, &pre })
            {
                button->onClick = [&presses] { ++presses; };
                open();
                require (popup.isVisible() && juce::Component::getCurrentlyModalComponent() == nullptr
                             && ! button->isCurrentlyBlockedByAnotherModalComponent()
                             && parent.getLocalBounds().contains (popup.getBounds())
                             && ! popup.getBounds().intersects (button->getBounds()),
                         "the nonmodal MATCH choice leaves footer controls reachable");
                require (popup.bodyFont().getHeight() >= 14.0f, "MATCH choice text is readable");
                text_style::ShownTextLog painted;
                const auto image = popup.createComponentSnapshot (popup.getLocalBounds());
                const auto compact = [] (juce::String value) { return value.removeCharacters (" \t\r\n"); };
                require (image.isValid() && compact (painted.texts().joinIntoString (""))
                    == compact (text_style::shownText (heading) + text_style::shownText (lower) + text_style::shownText (limit)),
                    "both choices and the heading are painted in full");
                require (text_style::shownWrappedHeight (heading, popup.bodyFont(), popup.titleBounds().getWidth())
                             <= popup.titleBounds().getHeight() + 0.5f,
                         "MATCH heading fits its space");
                popup.dismissIfOutside (button);
                button->onClick();
                require (! popup.isVisible() && presses > 0 && chosen == 0,
                         "an outside action closes the choice and is delivered once");
            }
            require (presses == 3, "each footer action runs once");
            for (int index : { 1, 2 })
            {
                open();
                popup.dismissIfOutside (&popup.option (index));
                require (popup.isVisible(), "a choice press is not dismissed as an outside press");
                popup.option (index).onClick();
                require (! popup.isVisible() && chosen == index, "each explicit MATCH choice is delivered once");
            }
            open();
            parent.setSize (width + 1, width * 2 / 3);
            require (! popup.isVisible(), "resizing dismisses a stale choice");
        }
    }
}

template <class Require> void verifyRecoveryGuidance (Require require)
{
    verifyMatchChoicePopup (require);
    using namespace live_compare;
    using namespace live_compare_ui;
    for (auto reason : { RecoveryReason::reaperFirstCycle, RecoveryReason::reaperDelayUnverified,
                        RecoveryReason::loopWaiting, RecoveryReason::loopTooShort })
    for (auto restriction : { ReaperChainRestriction::none, ReaperChainRestriction::unavailable })
    {
        Status named; named.active = named.preSelected = named.preWaiting = true;
        named.observation = reason; named.reaperRestriction = restriction;
        LiveBlindStatus blind; blind.stage = BlindStage::preparing;
        blind.observation = reason; blind.reaperRestriction = restriction;
        require (juce::String (namedRecovery (named)).isEmpty()
            && !live_blind_ui::screenFor (blind, true, 1).guidanceShown, "no guidance for a short recovery");
        named.prolongedWait = blind.prolongedWait = true;
        const juce::String text = namedRecovery (named);
        require (text.isNotEmpty() && text == blindRecovery (blind, false).instruction,
                 "LISTEN and BLIND explain the same long recovery the same way");
        if (reason == RecoveryReason::loopWaiting || (reason == RecoveryReason::reaperFirstCycle
            && restriction == ReaperChainRestriction::none))
            require (text == "Waiting for the first loop pass (POST)", "a first pass never asks for a longer loop");
        if (restriction == ReaperChainRestriction::unavailable
            && (reason == RecoveryReason::reaperFirstCycle || reason == RecoveryReason::reaperDelayUnverified))
            require (text.contains ("turn loop off") && !text.contains ("longer"), "API failure has an applicable remedy");
        named.preSelected = named.preWaiting = false;
        require (juce::String (namedRecovery (named)).isEmpty(), "POST selection has no PRE timing notice");
    }
    {
        Status waiting;
        waiting.active = waiting.preSelected = waiting.preWaiting = waiting.prolongedWait = true;
        waiting.observation = RecoveryReason::notWritten;
        const auto presentation = namedPresentation (waiting);
        require (presentation.action == RecoveryAction::automatic
                     && juce::String (presentation.instruction) == "No new PRE processing; DAW may pause it (POST)",
                 "a silent host can pause PRE callbacks without disabling PRE");
    }
    for (auto loop : { LoopPairingBlock::auCycle, LoopPairingBlock::aaxLoop })
    {
        Status named; named.active = named.prolongedWait = true; named.loopBlock = loop;
        require (juce::String (namedRecovery (named)).isEmpty(), "POST does not ask to disable a loop");
    }
    Status named; named.active = named.signalWait = true;
    LiveBlindStatus blind; blind.stage = BlindStage::preparing; blind.signalWait = blind.prolongedWait = true;
    const auto screen = live_blind_ui::screenFor (blind, true, 1);
    require (screen.cause.isEmpty() && screen.recovery == namedRecovery (named)
        && screen.recovery == "MATCH waits for more signal", "signal wait is one matching line, without an invented timing fault");
}
}
