#pragma once

#include "../src/HyphaLanguage.h"
#include "../src/HyphaNoteDialog.h"
#include "../src/HyphaTextLookAndFeel.h"
#include "../src/HyphaTheme.h"
#include "../src/HyphaUiContract.h"

#include <cstdlib>
#include <iostream>

// 2026-10-18: the NOTE window shows ADD and CANCEL whole and reachable at every editor
// size, display scale and language: each button lies inside the window and is what a press at its
// centre hits, the memo takes typed text (Japanese too), Return presses ADD and Escape CANCEL.
namespace hypha::tests
{
inline void verifyNoteDialog()
{
    const auto require = [] (bool valid, const juce::String& message)
    {
        if (valid) return;
        std::cerr << "NOTE dialog: " << message << '\n';
        std::exit (EXIT_FAILURE);
    };
    TextLookAndFeel lookAndFeel;
    int checked = 0;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto scale : { 1.0f, 1.25f, 1.5f, 2.0f })
            for (const auto& size : ui_contract::spectrumSizePresets)
            {
                juce::Component owner;
                owner.setSize (size.width, size.height);
                owner.setTransform (juce::AffineTransform::scale (scale));  // a host's display scale
                const auto context = presentation::forEditor (size.width, size.height);
                auto dialog = note_dialog::create (owner, lookAndFeel, context);
                const auto where = juce::String (size.width) + "x" + juce::String (size.height) + " @" + juce::String (scale)
                                 + (language == i18n::Language::japanese ? " ja" : " en");
                require (dialog->getNumButtons() == 2, "ADD and CANCEL: " + where);
                dialog->setVisible (true);  // shown, as enterModalState shows it (not added to the desktop here)
                for (int index = 0; index < dialog->getNumButtons(); ++index)
                {
                    auto* button = dialog->getButton (index);
                    require (dialog->getLocalBounds().contains (button->getBounds()),
                             "button " + button->getButtonText() + " lies whole inside the window: " + where
                                 + " button " + button->getBounds().toString() + " window " + dialog->getLocalBounds().toString());
                    require (dialog->getComponentAt (button->getBounds().getCentre()) == button,
                             "a press at the centre of " + button->getButtonText() + " reaches it: " + where);
                }
                auto* memo = dialog->getTextEditor ("memo");
                require (memo != nullptr && dialog->getLocalBounds().contains (memo->getBounds())
                             && memo->getHeight() >= juce::roundToInt (memo->getFont().getHeight()),
                         "the memo field is inside the window and tall enough for its text: " + where);
                memo->insertTextAtCaret (juce::String::fromUTF8 (u8"Chorus 2 の頭"));
                require (dialog->getTextEditorContents ("memo") == juce::String::fromUTF8 (u8"Chorus 2 の頭"),
                         "the memo takes typed text: " + where);
                require (dialog->getButton ("ADD") != nullptr && dialog->getButton ("CANCEL") != nullptr
                             && dialog->getButton ("ADD")->isRegisteredForShortcut (juce::KeyPress (juce::KeyPress::returnKey))
                             && dialog->getButton ("CANCEL")->isRegisteredForShortcut (juce::KeyPress (juce::KeyPress::escapeKey)),
                         "Return presses ADD and Escape presses CANCEL: " + where);
                ++checked;
            }
    }
    std::cout << "NOTE dialog: " << checked << " size/scale/language cases reach ADD, CANCEL and the memo\n";
}
}
