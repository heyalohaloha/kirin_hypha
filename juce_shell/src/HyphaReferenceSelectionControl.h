#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace hypha::reference_ui
{
// An explicit choice can renew a stale source even when its parent ID is unchanged.
// Model refreshes still use dontSendNotification and never perform this action.
class SelectionControl final : public juce::ComboBox
{
public:
    void choose (int id)
    {
        if (! isEnabled() || id <= 0 || ! isItemEnabled (id)) return;
        if (id != getSelectedId()) setSelectedId (id, juce::sendNotificationSync);
        else if (onChange) onChange();
    }
    // A mouse click or Return marks the base ComboBox's list active and opens it only while it is
    // not; hidePopup() clears that mark, as the base class does when its own list closes.
    void showPopup() override
    {
        if (showing) return;
        if (! isEnabled() || getNumItems() == 0) { hidePopup(); return; }
        showing = true;
        juce::PopupMenu menu;
        std::vector<juce::String> labels;
        for (int index = 0; index < getNumItems(); ++index)
        {
            const auto id = getItemId (index);
            labels.push_back (getItemText (index));
            menu.addItem (id, labels.back(), isItemEnabled (id), id == getSelectedId());
        }
        menu.setLookAndFeel (&getLookAndFeel());
        const juce::Component::SafePointer<SelectionControl> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withMinimumWidth (getWidth()).withItemThatMustBeVisible (getSelectedId()),
            [safe, offeredLabels = std::move (labels)] (int id) {
                if (safe == nullptr) return;
                safe->showing = false;
                safe->hidePopup();
                const auto index = safe->indexOfItemId (id);
                if (index >= 0 && index < static_cast<int> (offeredLabels.size())
                    && safe->getItemText (index) == offeredLabels[static_cast<std::size_t> (index)])
                    safe->choose (id);
            });
    }
private:
    bool showing = false;
};
}
