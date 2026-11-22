#pragma once

#include "../src/HyphaBlindScreen.h"
#include "../src/HyphaLanguage.h"
#include "ShortcutKeyRig.h"
#include <chrono>

namespace hypha::tests::blind_notice_focus_test
{
class DisclosureWait final : private juce::Timer
{
public:
    bool afterFormerTooltipTicks()
    {
        using Clock = std::chrono::steady_clock;
        const auto deadline = Clock::now() + std::chrono::seconds (3);
        startTimer (330);  // Longer than two stock 123 ms tooltip ticks.
        while (!elapsed && Clock::now() < deadline)
        {
           #if JUCE_MAC
            const auto remaining = std::chrono::duration<double> (deadline - Clock::now()).count();
            if (remaining > 0) CFRunLoopRunInMode (kCFRunLoopDefaultMode, remaining, true);
           #else
            juce::detail::dispatchNextMessageOnSystemQueue (false);
           #endif
        }
        stopTimer();
        return elapsed;
    }

private:
    void timerCallback() override { elapsed = true; stopTimer(); }
    bool elapsed = false;
};

template <typename Require>
void verify (ShortcutKeyRig& rig, const blind_ui::ScreenComponent& activeScreen, Require require)
{
    const i18n::ScopedLanguage language (i18n::Language::japanese);
    // A native editor tick closes the injected shortcut trial because it has no audio session.
    // Host the same shipping screen with a stable fixture model for the timed focus assertions.
    auto screen = activeScreen.screen();
    blind_ui::ScreenComponent blind ("live-blind");
    juce::DocumentWindow window ("Notices", juce::Colours::black, 0);
    screen.trialNotice = juce::String (live_compare_ui::unappliedNotice()) + "\nRepeated audio: checked per block";
    blind.setSize (300, 200);
    blind.setScreen (screen);
    window.setContentNonOwned (&blind, true);
    window.centreWithSize (window.getWidth(), window.getHeight());
    window.setVisible (true);
    window.toFront (true);
    auto* peer = blind.getPeer();
    require (peer != nullptr, "the notice screen is attached to a native editor peer");
    const auto press = [peer] (int code)
    {
        const auto handled = peer->handleKeyPress (juce::KeyPress (code));
        peer->handleKeyUpOrDown (false);
        return handled;
    };
    auto* badge = dynamic_cast<juce::Button*> (blind.findChildWithID ("live-blind-more-notices"));
    auto* disclosure = blind.findChildWithID ("live-blind-more-notices-disclosure");
    auto* first = blind.findChildWithID ("live-blind-source-1");
    require (badge && badge->isShowing() && first && disclosure, "Japanese narrow BLIND has a real notice badge");

    const auto editorBounds = blind.getScreenBounds();
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (editorBounds);
    require (display != nullptr, "the hosted BLIND fixture has a display");
    const auto corners = display->userArea.reduced (4);
    const auto originalMouse = juce::Desktop::getMousePosition();
    struct RestorePointer { juce::Point<int> point; ~RestorePointer() { juce::Desktop::setMousePosition (point); } } restore { originalMouse };
    bool movedOutside = false;
    for (const auto point : { corners.getTopLeft(), corners.getTopRight(), corners.getBottomLeft(), corners.getBottomRight() })
    {
        if (editorBounds.contains (point)) continue;
        juce::Desktop::setMousePosition (point);
        movedOutside = !editorBounds.contains (juce::Desktop::getMousePosition());
        if (movedOutside) break;
    }
    require (movedOutside, "pointer initially outside the plug-in editor");

    const auto tabToBadge = [&]
    {
        first->grabKeyboardFocus();
        for (int tab = 0; tab < blind.getNumChildComponents() && !badge->hasKeyboardFocus (false); ++tab)
            require (press (juce::KeyPress::tabKey), "Tab is dispatched by the host peer");
        require (badge->hasKeyboardFocus (false), "Tab reaches the actual notice badge");
    };
    tabToBadge();
    require (disclosure->isShowing() && disclosure->getName() == badge->getDescription()
             && blind.getLocalBounds().contains (disclosure->getBounds()),
             "keyboard focus exposes exactly the hidden text in the editor");
    DisclosureWait wait;
    require (wait.afterFormerTooltipTicks() && badge->hasKeyboardFocus (false) && disclosure->isShowing()
             && !editorBounds.contains (juce::Desktop::getMousePosition()),
             "keyboard disclosure persists through stock tooltip ticks with no pointer over the editor");
    require (press (juce::KeyPress::escapeKey) && badge->hasKeyboardFocus (false) && !disclosure->isShowing(),
             "Escape dismisses the disclosure without losing the badge");
    require (press (juce::KeyPress::tabKey) && !badge->hasKeyboardFocus (false) && !disclosure->isShowing(),
             "Tab moves on after an explicit close");
    tabToBadge();
    require (disclosure->isShowing() && press (juce::KeyPress::tabKey) && !disclosure->isShowing(),
             "moving keyboard focus closes an open disclosure");
    tabToBadge();
    blind.setCovered (true);
    require (!badge->isShowing() && !disclosure->isShowing(), "removing the badge clears the disclosure");
    window.clearContentComponent();
    window.setVisible (false);
    rig.window->toFront (true);
}
}
