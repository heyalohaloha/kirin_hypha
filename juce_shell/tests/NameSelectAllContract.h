#pragma once
#include "../src/HyphaNameTextEditor.h"
#include "../src/HyphaWidgets.h"
#include <stdexcept>
namespace hypha::tests {
inline void verifyNameSelectAll()
{
    const auto require = [](bool ok) { if (!ok) throw std::runtime_error("name select-all contract"); };
    EditableName name; name.setSize (180, 28); name.setModelName ("Old Name");
    require (name.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
    NameTextEditor* editor = nullptr;
    for (auto* child : name.getChildren())
        if (auto* value = dynamic_cast<NameTextEditor*> (child)) editor = value;
    require (editor != nullptr && name.isEditing());
    const auto ctrl = juce::ModifierKeys (juce::ModifierKeys::ctrlModifier);
    for (const auto text : {juce::String ("Old Name"), juce::String::fromUTF8 (u8"名前"), juce::String()})
    {
        editor->setText (text, false); editor->moveCaretToEnd();
        require (consumeNameSelectAll (*editor, true, true, ctrl, true));
        require (editor->getHighlightedText() == text);
        editor->insertTextAtCaret ("X"); require (editor->getText() == "X");
    }
    for (int mask : {0, juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier,
                       juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier})
    {
        editor->setText ("Keep", false); editor->moveCaretToEnd();
        require (!consumeNameSelectAll (*editor, true, true, juce::ModifierKeys (mask), true));
        require (editor->getHighlightedText().isEmpty());
    }
    require (!consumeNameSelectAll (*editor, false, true, ctrl, true));
    require (!consumeNameSelectAll (*editor, true, false, ctrl, true));
    require (!consumeNameSelectAll (*editor, true, true, ctrl, false));
    editor->setReadOnly (true); require (!consumeNameSelectAll (*editor, true, true, ctrl, true));
    editor->setReadOnly (false); editor->setText ("Mac stays native", false); editor->moveCaretToEnd();
    require (editor->keyPressed (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0)));
    require (editor->getHighlightedText() == "Mac stays native");
}
}
