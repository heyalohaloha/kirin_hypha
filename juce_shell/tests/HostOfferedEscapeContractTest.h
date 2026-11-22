#pragma once

#include "ShortcutKeyRig.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
 #include "../src/HyphaBlindImageSheet.h"
#endif

// Esc while something of the window's own is open. A host either sends the native press (through
// the peer, as the rig does) or asks the view first and sends nothing after a "used" answer
// (VST3 IPlugView::onKeyDown, JUCE patch 0015). Both must close only what is open and keep the key
// from the host; with nothing open, and with a modifier, an offered Esc stays the host's.
namespace hypha::tests::offered_escape_test
{
inline bool offer (ShortcutKeyRig& rig, bool down, int modifiers = 0, int code = juce::KeyPress::escapeKey)
{
    return rig.editor->kirinHostKeyOffered (juce::KeyPress (code, juce::ModifierKeys (modifiers), 0), down);
}

template <typename Require>
void verifyKeysList (ShortcutKeyRig& rig, Require require)
{
    rig.editor->grabKeyboardFocus();
    require (! offer (rig, true) && ! offer (rig, false), "an offered Esc with nothing to close stays the host's");
    auto* before = rig.view().findChildWithID ("observatory-time");
    before->grabKeyboardFocus();
    rig.press ('F');
    require (rig.keys().isVisible(), "F shows the list before an offered Esc");
    require (! offer (rig, true, 0, 'F') && rig.keys().isVisible(), "only Esc is taken from a host's offer");
    require (! offer (rig, true, juce::ModifierKeys::commandModifier) && rig.keys().isVisible(),
             "an offered Esc with a modifier stays the host's and closes nothing");
    require (offer (rig, true) && ! rig.keys().isVisible() && before->hasKeyboardFocus (false),
             "an offered Esc closes the list, returns the focus and is not the host's");
    require (offer (rig, true) && offer (rig, false), "the repeat and the release of a used Esc are not the host's");
    require (! offer (rig, true) && ! offer (rig, false), "the next Esc is the host's again");
    // No native key-up follows an offered press: the next offered Esc is a new press.
    rig.hold ('F');
    require (rig.keys().isVisible() && offer (rig, true) && ! rig.keys().isVisible() && offer (rig, false),
             "a second offered Esc closes the list again");
    rig.up();
    rig.editor->grabKeyboardFocus();
}

#if ! KIRIN_HYPHA_PRE_DISPLAY
// The SAVE IMAGE sheet over the BLIND RESULT, on the editor's own Blind screen.
template <typename Require>
void verifyImageSheet (ShortcutKeyRig& rig, live_blind_ui::Component& blind, Require require)
{
    const auto remembered = blind.preferenceFile();
    const juce::TemporaryFile preferences ("ui-preferences.txt");
    blind.setPreferenceFile (preferences.getFile());
    blind_ui::Screen screen;
    screen.saveImageShown = true;
    const auto open = [&]
    {
        blind.setScreen (screen);
        blind.setVisible (true);
        blind.openImageSheet();
        require (blind.imageSheetOpen() && blind.imageSheet().nameField (false).hasKeyboardFocus (true),
                 "SAVE IMAGE opens its sheet with the first name holding the keys");
    };
    const auto closedWithFocus = [&]
    { return ! blind.imageSheetOpen() && blind.hasKeyboardFocus (true) && rig.editor->hasKeyboardFocus (true); };

    open();
    for (auto modifier : { juce::ModifierKeys::commandModifier, juce::ModifierKeys::ctrlModifier,
                           juce::ModifierKeys::altModifier, juce::ModifierKeys::shiftModifier })
        require (! offer (rig, true, modifier) && ! offer (rig, false, modifier) && blind.imageSheetOpen(),
                 "an offered Esc with a modifier stays the host's and leaves the sheet open");

    blind.imageSheet().grabKeyboardFocus();  // a click on the preview
    require (rig.press (juce::KeyPress::escapeKey) && closedWithFocus(),
             "a native Esc on the preview closes only the sheet and gives the keys back to the window");

    open();
    blind.imageSheet().grabKeyboardFocus();
    require (offer (rig, true) && closedWithFocus(), "an offered Esc on the preview closes only the sheet");
    require (offer (rig, true) && offer (rig, false), "its repeat and release are not the host's");
    require (! offer (rig, true) && ! offer (rig, false), "with the sheet gone Esc is the host's again");

    open();
    juce::Component::unfocusAllComponents();
    require (juce::Component::getCurrentlyFocusedComponent() == nullptr, "the keys can be outside the window");
    require (offer (rig, true) && closedWithFocus() && offer (rig, false),
             "an offered Esc closes the sheet from outside the window's focus and takes the keys back");

    // A name field answers the press at once and closes the sheet with its next message.
    open();
    require (offer (rig, true) && blind.imageSheetOpen(), "an offered Esc in a name is used by the name");
    require (afterQueuedMessages ([] {}) && ! blind.imageSheetOpen(),
             "and the sheet closes without the host being given the key");
    offer (rig, false);
    blind.setPreferenceFile (remembered);
}

// The same sheet on a Blind screen of its own (no editor tick): a native Esc in a name.
template <typename Require>
void verifyImageSheetName (ShortcutKeyRig& rig, Require require)
{
    const juce::TemporaryFile preferences ("ui-preferences.txt");
    live_blind_ui::Component blind;
    blind.setPreferenceFile (preferences.getFile());
    blind_ui::Screen screen;
    screen.saveImageShown = true;
    blind.setSize (900, 600);
    blind.setScreen (screen);
    juce::DocumentWindow window ("Image", juce::Colours::black, 0);
    window.setContentNonOwned (&blind, true);
    window.setVisible (true);
    window.toFront (true);
    auto* peer = blind.getPeer();
    require (peer != nullptr, "the Blind screen is on a native peer");
    blind.openImageSheet();
    auto& name = blind.imageSheet().nameField (false);
    require (blind.imageSheetOpen() && name.hasKeyboardFocus (true), "the first name holds the keys");
    require (peer->handleKeyPress (juce::KeyPress ('k', juce::ModifierKeys(), 'k')) && name.getText().endsWithChar ('k'),
             "the name takes text");
    const auto used = peer->handleKeyPress (juce::KeyPress (juce::KeyPress::escapeKey));
    peer->handleKeyUpOrDown (false);
    require (used && afterQueuedMessages ([] {}) && ! blind.imageSheetOpen() && blind.hasKeyboardFocus (true),
             "a native Esc in a name is used, closes only the sheet and returns the keys to the screen");
    window.clearContentComponent();
    window.setVisible (false);
    rig.window->toFront (true);
}
#endif
}
