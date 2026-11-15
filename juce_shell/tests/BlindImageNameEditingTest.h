#pragma once

#include "../src/HyphaBlindImageSheet.h"
#include <stdexcept>

namespace hypha::tests
{
inline void verifyBlindImageNameEditing()
{
    blind_image::Sheet sheet ("test");
    blind_image::Facts facts;
    facts.nameA = "PRE";
    facts.nameB = "POST";
    sheet.open (facts);
    for (const bool second : { false, true })
    {
        auto& field = sheet.nameField (second);
        auto& component = static_cast<juce::Component&> (field);
        for (const auto cause : { juce::Component::focusChangedByTabKey, juce::Component::focusChangedByMouseClick,
                                  juce::Component::focusChangedDirectly })
        {
            field.setText (second ? "POST" : "PRE", false);
            component.focusGained (cause);
            if (field.getHighlightedRegion() != juce::Range<int> (0, field.getText().length()))
                throw std::runtime_error ("focus must select the whole image name");
            field.insertTextAtCaret ("New");
            if (field.getText() != "New") throw std::runtime_error ("typing must replace the focused name");
        }
        field.setText ("Longer name", false);
        field.setCaretPosition (3);
        if (! component.keyPressed (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
            || field.getHighlightedRegion() != juce::Range<int> (0, field.getText().length()))
            throw std::runtime_error ("platform select-all shortcut must select the whole name");
        field.insertTextAtCaret ("X");
        if (field.getText() != "X") throw std::runtime_error ("typing must replace the selected name");
    }
}
}
