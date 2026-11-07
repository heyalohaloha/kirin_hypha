#pragma once
#include "LiveCompareNoticeProductChecks.h"
#include <juce_gui_basics/juce_gui_basics.h>

// LISTEN from the keyboard with real PRE and POST: the key presses reach the shipping editor as the
// window receives them, and what changes is what the processors report.
namespace hypha::tests::editor_product
{
inline void verifyListenFromKeys()
{
    NoticeRig r;
    r.advance (0.1);
    r.selectPair();
    juce::DocumentWindow window ("Keys", juce::Colours::black, juce::DocumentWindow::minimiseButton);
    window.setContentNonOwned (r.editor.get(), true);
    window.setVisible (true);
    require (r.editor->getPeer() != nullptr && r.editor->isShowing(), "native window for the keys");
    r.editor->grabKeyboardFocus();
    require (r.editor->hasKeyboardFocus (true), "the window has the keys");
    const auto press = [&] (int code, int modifiers = 0)
    {
        const auto used = r.editor->getPeer()->handleKeyPress (juce::KeyPress (code, juce::ModifierKeys (modifiers), 0));
        r.editor->getPeer()->handleKeyUpOrDown (false);
        return used;
    };
    const auto shift = juce::ModifierKeys::shiftModifier;
    r.advance (0.2);
    require (press ('Q') && r.post->liveCompareStatus().active, "Q starts LISTEN");
    r.expectOrdinaryAudio = false;
    r.advance (0.6);
    require (press ('1'), "1 is used in LISTEN");
    r.advance (1.0);
    require (r.post->liveCompareStatus().preSelected && r.post->liveCompareStatus().preAudible, "1 plays PRE");
    require (press ('2'), "2 is used in LISTEN");
    r.advance (0.1);
    require (! r.post->liveCompareStatus().preSelected, "2 plays POST");
    // PRE and POST stay on the keys from any page, Reference included.
    require (press ('5', shift) && r.view().domain() == observatory::Domain::reference, "Shift+5 opens REF");
    require (press ('1'), "1 is used on REF");
    r.advance (0.1);
    require (r.post->liveCompareStatus().preSelected, "1 is still PRE on REF");
    require (! press (juce::KeyPress::escapeKey) && r.post->liveCompareStatus().active, "Esc is not END");
    require (press ('Q') && r.post->liveCompareStatus().active, "Q again leaves LISTEN running");
    require (press ('1', shift) && r.view().domain() == observatory::Domain::level, "Shift+1 returns to LEVEL");
    r.advance (3.2);
    for (int n = 0; n < 100 && ! r.post->measureLiveCompare().ok(); ++n) r.advance (0.1);
    require (press ('G'), "G is MATCH");
    r.advance (0.2);
    for (int n = 0; n < 20 && ! r.post->liveCompareStatus().matched; ++n) r.advance (0.1);
    require (r.post->liveCompareStatus().matched, "G matches");
    require (press ('G', shift) && juce::Component::getNumCurrentlyModalComponents() > 0, "Shift+G opens MATCH / AUTO");
    juce::PopupMenu::dismissAllActiveMenus();
    for (int n = 0; n < 40 && juce::Component::getNumCurrentlyModalComponents() > 0; ++n)
        require (afterQueuedMessages ([] {}), "the menu's close finishes");
    r.editor->grabKeyboardFocus();
    require (press ('T'), "T is used in LISTEN");
    const auto ended = r.post->liveCompareStatus();
    require (! ended.active || ended.finishing, "T ends LISTEN");
    r.advance (0.3);
    window.clearContentComponent();
}
}
