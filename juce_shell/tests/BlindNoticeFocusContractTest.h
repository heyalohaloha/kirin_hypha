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

inline juce::Rectangle<int> placementOnDisplay (juce::Rectangle<int> available, juce::Rectangle<int> window,
                                                juce::Point<int> pointer, int inset, int pointerClearance)
{
    const auto area = available.reduced (inset);
    if (area.getWidth() < window.getWidth() || area.getHeight() < window.getHeight()) return {};
    for (const auto x : { area.getX(), area.getRight() - window.getWidth() })
    for (const auto y : { area.getY(), area.getBottom() - window.getHeight() })
    {
        const auto candidate = window.withPosition (x, y);
        if (!candidate.expanded (pointerClearance).contains (pointer)) return candidate;
    }
    return {};
}

template <typename Require>
void verify (ShortcutKeyRig& rig, const blind_ui::ScreenComponent& activeScreen, Require require)
{
    const i18n::ScopedLanguage language (i18n::Language::japanese);
    // A native editor tick closes the injected shortcut trial because it has no audio session.
    // Host the same shipping screen with a stable fixture model for the timed focus assertions.
    auto screen = activeScreen.screen();
    blind_ui::ScreenComponent blind ("live-blind");
    juce::DocumentWindow window ("Notices", juce::Colours::black, 0);
    // Four notices overflow 300x200 with any platform font, so the badge path runs everywhere.
    screen.trialNotice = juce::String (live_compare_ui::unappliedNotice()) + "\nRepeated audio: checked per block\n"
        + live_compare_ui::notRemeasuredNotice() + "\n" + live_compare_ui::unmeasuredNotice();
    blind.setSize (300, 200);
    blind.setScreen (screen);
    window.setContentNonOwned (&blind, true);
    const auto pointer = juce::Desktop::getMousePosition();
    const auto displays = juce::Desktop::getInstance().getDisplays().getRectangleList (true);
    const juce::Rectangle<int> smallDisplay (0, 0, 600, 400), fixtureWindow (0, 0, 302, 228);
    const juce::Point<int> centredPointer (310, 200);
    const auto tight = placementOnDisplay (smallDisplay, fixtureWindow, centredPointer, 0, 0);
    require (placementOnDisplay (smallDisplay, fixtureWindow, centredPointer, 8, 4).isEmpty()
             && !tight.isEmpty() && smallDisplay.contains (tight) && !tight.contains (centredPointer),
             "the compact-display fallback fits while leaving the real pointer untouched");

    const auto findPosition = [&] (int inset, int clearance)
    {
        for (const auto display : displays)
            if (const auto fit = placementOnDisplay (display, window.getLocalBounds(), pointer, inset, clearance);
                !fit.isEmpty()) return fit;
        return juce::Rectangle<int>();
    };
    auto placement = findPosition (8, 4);
    if (placement.isEmpty()) placement = findPosition (0, 0);
    require (!placement.isEmpty(), "fixture window fits a screen area outside the untouched pointer");
    window.setBounds (placement);
    window.setVisible (true);
    window.toFront (true);
    auto* peer = blind.getPeer();
    require (peer != nullptr, "the notice screen is attached to a native editor peer");
    const auto press = [peer] (int code, int modifiers = 0)
    {
        const auto handled = peer->handleKeyPress (juce::KeyPress (code, juce::ModifierKeys (modifiers), 0));
        peer->handleKeyUpOrDown (false);
        return handled;
    };
    auto* badge = dynamic_cast<juce::Button*> (blind.findChildWithID ("live-blind-more-notices"));
    auto* disclosure = blind.findChildWithID ("live-blind-more-notices-disclosure");
    auto* first = blind.findChildWithID ("live-blind-source-1");
    require (badge && badge->isShowing() && first && disclosure, "Japanese narrow BLIND has a real notice badge");

    const auto editorBounds = blind.getScreenBounds();
    require (!editorBounds.contains (juce::Desktop::getMousePosition()), "pointer initially outside the plug-in editor");

    const auto tabToBadge = [&]
    {
        first->grabKeyboardFocus();
        for (int tab = 0; tab < blind.getNumChildComponents() && !badge->hasKeyboardFocus (false); ++tab)
            require (press (juce::KeyPress::tabKey), "Tab is dispatched by the host peer");
        require (badge->hasKeyboardFocus (false), "Tab reaches the actual notice badge");
    };
    tabToBadge();
    const auto* accessibility = badge->getAccessibilityHandler();
    require (accessibility && accessibility->getRole() == juce::AccessibilityRole::button
             && accessibility->getActions().contains (juce::AccessibilityActionType::press)
             && accessibility->getDescription().isNotEmpty()
             && accessibility->getDescription() == disclosure->getName()
             && accessibility->getHelp() == disclosure->getName(),
             "the real badge exposes all revealed text in both accessibility Description and Help");
    require (disclosure->isShowing() && disclosure->getName() == badge->getDescription()
             && blind.getLocalBounds().contains (disclosure->getBounds()),
             "keyboard focus exposes exactly the hidden text in the editor");
    // The hosted editor still has its own BLIND open. Its next layout (a language or size change) must
    // not take the keys from this window, nor from a control inside its own screen.
    rig.editor->resized();
    require (badge->hasKeyboardFocus (false) && disclosure->isShowing(),
             "another editor laying out its open BLIND leaves the keys and the notice text in this window");
    auto* hostedFirst = activeScreen.findChildWithID ("live-blind-source-1");
    require (hostedFirst != nullptr && activeScreen.isShowing(), "the hosted BLIND is still open beside the fixture");
    hostedFirst->grabKeyboardFocus();
    rig.editor->resized();
    require (hostedFirst->hasKeyboardFocus (false), "an editor laying out its open BLIND keeps the keys on the chosen control");
    juce::Component::unfocusAllComponents();
    require (juce::Component::getCurrentlyFocusedComponent() == nullptr, "the host may have focus outside JUCE");
    rig.editor->resized();
    require (juce::Component::getCurrentlyFocusedComponent() == nullptr,
             "laying out open BLIND does not take the keys when no JUCE component holds them");
    tabToBadge();
    require (disclosure->isShowing(), "returning to the badge shows the notice text again");
    DisclosureWait wait;
    require (wait.afterFormerTooltipTicks() && badge->hasKeyboardFocus (false) && disclosure->isShowing()
             && !editorBounds.contains (juce::Desktop::getMousePosition()),
             "keyboard disclosure persists through stock tooltip ticks with no pointer over the editor");
    for (auto modifier : { juce::ModifierKeys::commandModifier, juce::ModifierKeys::ctrlModifier,
                           juce::ModifierKeys::altModifier, juce::ModifierKeys::shiftModifier })
        require (!press (juce::KeyPress::escapeKey, modifier) && badge->hasKeyboardFocus (false)
                 && disclosure->isShowing(), "modified Escape is left for the host without hiding notice text");
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
