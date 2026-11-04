#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace hypha::reference_ui
{
// An explicit choice can renew a stale source even when its parent ID is unchanged.
// Model refreshes still use dontSendNotification and never perform this action.
class SelectionControl final : public juce::ComboBox
{
public:
    struct Row
    {
        juce::String key;       // stays the same while the row's text changes
        bool elsewhere = false; // listed under `elsewhereTitle`, not with the first rows
    };

    void setRows (std::vector<Row> next) { rows = std::move (next); }
    void setElsewhereTitle (const juce::String& title) { elsewhereTitle = title; }

    void choose (int id)
    {
        if (! isEnabled() || id <= 0 || ! isItemEnabled (id)) return;
        if (id != getSelectedId()) setSelectedId (id, juce::sendNotificationSync);
        else if (onChange) onChange();
    }
    // The row a press in an open list meant. A row keeps its key while its text changes (the AUTO
    // mark moves every few seconds), and a list that lost the row does nothing.
    void chooseRow (const juce::String& key)
    {
        for (int index = 0; index < getNumItems(); ++index)
            if (rowKey (index) == key) { choose (getItemId (index)); return; }
    }
    juce::PopupMenu makeMenu() const
    {
        juce::PopupMenu menu, elsewhere;
        for (int index = 0; index < getNumItems(); ++index)
        {
            const auto id = getItemId (index);
            const bool away = index < static_cast<int> (rows.size()) && rows[static_cast<size_t> (index)].elsewhere;
            (away ? elsewhere : menu).addItem (id, getItemText (index), isItemEnabled (id), id == getSelectedId());
        }
        if (elsewhere.getNumItems() > 0) menu.addSubMenu (elsewhereTitle, elsewhere);
        return menu;
    }
    // A mouse click or Return marks the base ComboBox's list active and opens it only while it is
    // not; hidePopup() clears that mark, as the base class does when its own list closes.
    void showPopup() override
    {
        if (showing) return;
        if (! isEnabled() || getNumItems() == 0) { hidePopup(); return; }
        showing = true;
        auto menu = makeMenu();
        std::vector<std::pair<int, juce::String>> offered;
        for (int index = 0; index < getNumItems(); ++index)
            offered.emplace_back (getItemId (index), rowKey (index));
        menu.setLookAndFeel (&getLookAndFeel());
        const juce::Component::SafePointer<SelectionControl> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withMinimumWidth (getWidth()).withItemThatMustBeVisible (getSelectedId()),
            [safe, offered = std::move (offered)] (int id) {
                if (safe == nullptr) return;
                safe->showing = false;
                safe->hidePopup();
                for (const auto& [item, key] : offered)
                    if (item == id) { safe->chooseRow (key); break; }
            });
    }
private:
    juce::String rowKey (int index) const
    { return index < static_cast<int> (rows.size()) ? rows[static_cast<size_t> (index)].key : getItemText (index); }

    std::vector<Row> rows;
    juce::String elsewhereTitle;
    bool showing = false;
};
}
