#pragma once

#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaUpdateContract.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void verifyInformationContract()
{
    const auto require = [] (bool value, const char* message)
    {
        if (! value) { std::cerr << "Information: " << message << '\n'; std::exit (1); }
    };
    using namespace update_information;
    int launched = 0, copied = 0;
    const auto launch = [&] (std::string_view value)
    {
        ++launched;
        require (value.substr (0, 8) == "https://", "HTTPS only");
        require (value.find ('?') == value.npos, "no identity in query");
        return true;
    };
    const auto copy = [&] (std::string_view) { ++copied; };
    for (const auto action : { Action::hoverHelp, Action::downloadsEnglish, Action::downloadsJapanese,
                              Action::changes, Action::copyEnglish, Action::copyJapanese,
                              Action::copyChanges })
    {
        require (dispatch (action, true, launch, copy) == Outcome::blocked, "Blind blocks dispatch");
        require (launched == 0 && copied == 0, "blocked action has no external side effect");
    }
    require (dispatch (Action::none, false, launch, copy) == Outcome::ignored, "cancel");
    require (dispatch (Action::hoverHelp, false, launch, copy) == Outcome::hoverHelpRequested,
             "hover help remains explicit and is also blocked if Blind starts after opening");
    require (dispatch (static_cast<Action> (99), false, launch, copy) == Outcome::ignored,
             "unknown action cannot become a URL");
    require (dispatch (Action::downloadsEnglish, false, launch, copy) == Outcome::opened, "open");
    require (dispatch (Action::copyJapanese, false, launch, copy) == Outcome::copied, "copy");
    require (launched == 1 && copied == 1, "one side effect per action");
    require (dispatch (Action::changes, false, [] (std::string_view) { return false; }, copy)
                 == Outcome::openFailed, "browser failure is not update success");
    require (url (Action::copyJapanese) == url (Action::downloadsJapanese), "same fallback URL");
    for (const auto action : { Action::downloadsEnglish, Action::downloadsJapanese, Action::changes })
        require (copies (copyAction (action)) && url (copyAction (action)) == url (action),
                 "browser failure offers the exact requested URL");

    for (const auto role : { observatory::Role::pre, observatory::Role::post })
        for (int width = 300; width <= 900; width += 15)
        {
            observatory::View view (role);
            view.setSize (width, width * 2 / 3);
            view.setReferenceEnabled (false);
            auto& anchor = view.informationAnchor();
            require (anchor.isVisible() && anchor.isEnabled(), "all sizes / no license / no pair");
            require (anchor.getWantsKeyboardFocus(), "keyboard entry");
            require (anchor.getTitle() == "Hypha information", "accessible name");
            require (anchor.getWidth() >= 60 && anchor.getHeight() >= 20, "usable target");
            require (view.getLocalBounds().contains (anchor.getBounds()), "inside shell");
            require (! anchor.getBounds().intersects (view.connectionBounds()), "no pair overlap");
            for (auto* child : view.getChildren())
                if (child != &anchor && child->isVisible())
                    require (! anchor.getBounds().intersects (child->getBounds()), "no control overlap");
            bool invoked = false;
            view.onInformation = [&] { invoked = true; };
            auto* button = dynamic_cast<juce::Button*> (&anchor);
            require (button != nullptr, "native button semantics");
            button->onClick();
            require (invoked, "common action wiring");
        }
}
}
