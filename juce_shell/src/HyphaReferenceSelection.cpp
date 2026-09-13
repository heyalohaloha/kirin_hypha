#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
void Component::syncSelectionControl (juce::ComboBox& box,
                                      const std::vector<SelectionOption>& options,
                                      const juce::String& selectedId)
{
    bool same = box.getNumItems() == static_cast<int> (options.size());
    for (int index = 0; same && index < box.getNumItems(); ++index)
        same = box.getItemText (index) == options[static_cast<size_t> (index)].label;
    if (! same)
    {
        box.clear (juce::dontSendNotification);
        for (size_t index = 0; index < options.size(); ++index)
            box.addItem (options[index].label, static_cast<int> (index) + 1);
    }
    int selected = 0;
    for (size_t index = 0; index < options.size(); ++index)
        if (options[index].id == selectedId)
            selected = static_cast<int> (index) + 1;
    box.setSelectedId (selected, juce::dontSendNotification);
    box.setEnabled (options.size() > 1);
}

juce::String Component::selectedOptionId (const juce::ComboBox& box,
                                          const std::vector<SelectionOption>& options)
{
    const int index = box.getSelectedId() - 1;
    return index >= 0 && index < static_cast<int> (options.size())
        ? options[static_cast<size_t> (index)].id : juce::String {};
}

}
