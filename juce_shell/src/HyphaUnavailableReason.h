#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// A control that cannot be used now says why (2026-11-11). `set` disables the control and keeps the
// reason on it; a click on the disabled control shows the reason where the editor shows its notices.
// A reason says what is missing and what makes the control usable, in a few words of English that the
// Japanese catalog translates. scripts/check_unavailable_reasons.mjs keeps new code on this path.
namespace hypha::unavailable
{
inline const juce::Identifier& reasonKey()
{
    static const juce::Identifier key ("hyphaUnavailableReason");
    return key;
}

inline void set (juce::Component& control, bool available, const juce::String& reason)
{
    control.setEnabled (available);
    if (available || reason.isEmpty()) control.getProperties().remove (reasonKey());
    else control.getProperties().set (reasonKey(), reason);
}

// The reason of the nearest disabled control at or above the one clicked, or nothing.
inline juce::String reasonAt (const juce::Component* clicked)
{
    for (auto* component = clicked; component != nullptr; component = component->getParentComponent())
        if (! component->isEnabled())
            if (const auto* reason = component->getProperties().getVarPointer (reasonKey()))
                return reason->toString();
    return {};
}

// Hears every click inside the editor (added with nested events); a disabled control with a reason
// shows it. A control that can be used is left to its own handler.
class ClickReasons final : public juce::MouseListener
{
public:
    explicit ClickReasons (std::function<void (const juce::String&)> showReason) : show (std::move (showReason)) {}
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (const auto reason = reasonAt (event.originalComponent); reason.isNotEmpty() && show)
            show (reason);
    }

private:
    std::function<void (const juce::String&)> show;
};

// A menu item that cannot be chosen now carries its reason on the next line, so the menu says why.
inline void addItem (juce::PopupMenu& menu, int id, const juce::String& text, bool available,
                     const juce::String& reason, bool ticked = false)
{
    menu.addItem (id, text, available, ticked);
    if (! available && reason.isNotEmpty())
        menu.addItem (-1, reason, false, false);
}
}
