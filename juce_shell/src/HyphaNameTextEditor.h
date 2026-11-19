#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace hypha {
// The host must not take the select-all key-down before the name's WM_CHAR arrives.
// The decision is explicit so tests can exercise it without polling a physical keyboard.
inline bool consumeNameSelectAll (juce::TextEditor& editor, bool down, bool focused,
                                 juce::ModifierKeys modifiers, bool aDown)
{
    if (!down || !focused || !aDown || editor.isReadOnly()
        || modifiers.withoutMouseButtons() != juce::ModifierKeys (juce::ModifierKeys::ctrlModifier))
        return false;
    editor.selectAll();
    return true;
}
class NameTextEditor final : public juce::TextEditor
{
public:
    bool keyStateChanged (bool down) override
    {
       #if JUCE_WINDOWS
        if (consumeNameSelectAll (*this, down, hasKeyboardFocus (false),
                                  juce::ModifierKeys::currentModifiers,
                                  juce::KeyPress::isKeyCurrentlyDown ('A')))
            return true;
       #endif
        return juce::TextEditor::keyStateChanged (down);
    }
};
}
