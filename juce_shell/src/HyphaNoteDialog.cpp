#include "HyphaNoteDialog.h"

#include "HyphaLanguage.h"

namespace hypha::note_dialog
{
std::unique_ptr<juce::AlertWindow> create (juce::Component& owner, juce::LookAndFeel& lookAndFeel,
                                           const presentation::Context& context)
{
    // Built in the language shown; its buttons measure and draw through the editor's LookAndFeel,
    // and a note may be typed in Japanese, which the native font carries.
    auto dialog = std::make_unique<juce::AlertWindow> (
        "NOTE", i18n::tr ("Attach a note to the current sample position."),
        juce::MessageBoxIconType::NoIcon, &owner);
    dialog->setLookAndFeel (&lookAndFeel);
    dialog->addTextEditor ("memo", {}, "NOTE");
    if (auto* editor = dialog->getTextEditor ("memo"))
    {
        editor->setInputRestrictions (240);
        editor->setFont (nativeTextFont (context, typography::TextRole::body));
    }
    dialog->addButton ("ADD", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    // Centre it at the size JUCE laid it out for (title, text, memo and buttons); resizing it afterwards
    // does not move the buttons.
    dialog->centreAroundComponent (&owner, dialog->getWidth(), dialog->getHeight());
    return dialog;
}
}
