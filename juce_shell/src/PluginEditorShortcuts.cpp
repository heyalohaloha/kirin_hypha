#include "PluginEditor.h"

// The keys of this window: where a press goes, and what the window does with focus. The window has the
// keys once it has been clicked, and gives them up as any window does. Keys it does not use go back to
// the host, which also means a key the host takes first never arrives here.
void KirinHyphaEditor::configureShortcuts()
{
    keysPanel.onClose = [this] { closeKeysList(); };
    scaleRoot.addChildComponent (keysPanel);
}

bool KirinHyphaEditor::shortcutsListening() const
{
    if (! isShowing() || juce::Component::getNumCurrentlyModalComponents() > 0) return false;
    // A text field keeps everything it is given, IME composition included.
    return dynamic_cast<juce::TextInputTarget*> (juce::Component::getCurrentlyFocusedComponent()) == nullptr;
}

hypha::shortcuts::Context KirinHyphaEditor::shortcutContext() const
{
    hypha::shortcuts::Context context;
    context.keysListOpen = keysPanel.isVisible();
    context.time = observatoryDomain == hypha::observatory::Domain::time;
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    context.post = isPost;
    if (isPost)
    {
        const auto live = processorRef.liveCompareStatus();
        context.blind = blindRunning();
        context.listen = live.active && ! live.finishing;
        context.candidateCompareAvailable = true;
        context.candidateCompareActive = context.listen && processorRef.candidateCompareActive();
        context.reference = observatoryDomain == hypha::observatory::Domain::reference && referenceView.isVisible();
    }
   #endif
    return context;
}

bool KirinHyphaEditor::keyPressed (const juce::KeyPress& key)
{
    if (! shortcutsListening()) return false;
    const auto press = hypha::shortcuts::normalise (key);
    const auto result = hypha::shortcuts::resolve (press, shortcutContext());
    if (! result.assigned) return false;
    if (! heldShortcutKeys.press (press.key, key.getKeyCode())) return true;  // the same press, repeating
    if (result.binding != nullptr) performShortcut (result.binding->command);
    else showActionNotice (result.refusal);
    return true;
}

bool KirinHyphaEditor::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown) heldShortcutKeys.released();
   #if JUCE_WINDOWS
    // The key press comes with the WM_CHAR after this WM_KEYDOWN, which the host is sent unless it is
    // used here. A key the window will use must not reach the host as well.
    if (isKeyDown && shortcutsListening())
    {
        const auto context = shortcutContext();
        const auto mods = juce::ModifierKeys::currentModifiers.withoutMouseButtons();
        for (const auto& binding : hypha::shortcuts::table::bindings)
            if (juce::KeyPress::isKeyCurrentlyDown (binding.key)
                && hypha::shortcuts::resolve (hypha::shortcuts::normalise (juce::KeyPress (binding.key, mods, 0)), context).assigned)
                return true;
    }
   #endif
    return false;
}

// A click in the window gives it the keys, on a meter as on a button. A control does not keep them:
// Return and the keypad's Enter stay the host's until someone chooses a control with Tab. A text field
// and a graph that moves with the arrow keys do keep them.
void KirinHyphaEditor::mouseDown (const juce::MouseEvent&)
{
    if (! shortcutsListening()) return;
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    const bool control = dynamic_cast<juce::Button*> (focused) != nullptr
        || dynamic_cast<juce::ComboBox*> (focused) != nullptr
        || dynamic_cast<hypha::EditableName*> (focused) != nullptr;
    if (control || (focused != this && ! isParentOf (focused))) grabKeyboardFocus();
}

void KirinHyphaEditor::focusLost (FocusChangeType)
{
    if (! hasKeyboardFocus (true)) heldShortcutKeys.clear();
}

void KirinHyphaEditor::focusOfChildComponentChanged (FocusChangeType)
{
    if (! hasKeyboardFocus (true)) heldShortcutKeys.clear();
}

// Said where it can be seen: a Blind covers the footer, and its screen says it (without a name).
void KirinHyphaEditor::showActionNotice (const juce::String& english)
{
    if (english.isEmpty()) return;
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (localBlindOpen) { localBlindView.setActionNotice (english); return; }
    if (auto* screen = activeBlindScreen()) { screen->setActionNotice (english); return; }
   #endif
    showToast (english);
}

bool KirinHyphaEditor::refusedByBlind()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (blindRunning())
    {
        showActionNotice ("End Blind Compare first");
        return true;
    }
   #endif
    return false;
}

void KirinHyphaEditor::toggleKeysList()
{
    if (keysPanel.isVisible()) closeKeysList();
    else openKeysList();
}

void KirinHyphaEditor::openKeysList()
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    keysFocusReturn = isParentOf (focused) ? focused : nullptr;
    keysOpenedInBlind = shortcutContext().blind;
    tooltip.hideTip();
    keysPanel.setBounds (scaleRoot.getLocalBounds());
    keysPanel.setVisible (true);
    keysPanel.toFront (false);
    refreshKeysList();
    keysPanel.focusList();
}

void KirinHyphaEditor::closeKeysList()
{
    keysPanel.setVisible (false);
    if (keysFocusReturn != nullptr && keysFocusReturn->isShowing()) keysFocusReturn->grabKeyboardFocus();
    else if (isShowing()) grabKeyboardFocus();
    keysFocusReturn = nullptr;
}

// The list follows the window while it is open. A Blind that starts or ends under it closes it: the
// list says nothing of the page behind a Blind.
void KirinHyphaEditor::refreshKeysList()
{
    if (! keysPanel.isVisible()) return;
    const auto context = shortcutContext();
    if (context.blind != keysOpenedInBlind) closeKeysList();
    else keysPanel.setRows (hypha::shortcuts::listRows (context));
}
