#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
void Component::syncSelectionControl (juce::ComboBox& box,
                                      const std::vector<SelectionOption>& options,
                                      const juce::String& selectedId)
{
    const std::array<juce::ComboBox*, 5> boxes { &presetBox, &versionBox, &checkBox, &candidateBox, &cueBox };
    for (size_t i = 0; i < boxes.size(); ++i)
        if (boxes[i] == &box) selectionReadouts[i].setVisible (false);
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
    box.setEnabled (options.size() > 1 || (options.size() == 1 && selected == 0));
}

bool Component::selectionVisible (const juce::ComboBox& box) const
{
    const std::array<const juce::ComboBox*, 5> boxes { &presetBox, &versionBox, &checkBox, &candidateBox, &cueBox };
    for (size_t i = 0; i < boxes.size(); ++i)
        if (boxes[i] == &box) return box.isVisible() || selectionReadouts[i].isVisible();
    return false;
}

void Component::layoutSelectionReadouts()
{
    const std::array<juce::ComboBox*, 5> boxes { &presetBox, &versionBox, &checkBox, &candidateBox, &cueBox };
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        auto& box = *boxes[i]; auto& label = selectionReadouts[i];
        const bool shown = box.isVisible() || label.isVisible();
        const bool readOnly = box.getNumItems() == 1 && box.getSelectedId() > 0;
        label.setBounds (box.getBounds());
        label.setText (box.getText(), juce::dontSendNotification);
        label.setFont (displayTextFont (box.getText(), presentationContext, typography::TextRole::readout,
                                       typography::Composition::information));
        label.setTitle (box.getTitle());
        label.setTooltip (box.getText());
        label.setVisible (shown && readOnly);
        box.setVisible (shown && ! readOnly);
    }
}

juce::String Component::selectedOptionId (const juce::ComboBox& box,
                                          const std::vector<SelectionOption>& options)
{
    const int index = box.getSelectedId() - 1;
    return index >= 0 && index < static_cast<int> (options.size())
        ? options[static_cast<size_t> (index)].id : juce::String {};
}

}
