#pragma once
#include "HyphaReferenceSelectionControl.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaKeyLight.h"
#include "HyphaTextStyle.h"
#include "HyphaUnavailableReason.h"
#include "reference_audition/ReferenceTargetTypes.h"

namespace hypha::reference_ui
{
class TargetBar final : public juce::Component
{
    class Button final : public juce::TextButton
    {
    public:
        explicit Button (const char* text) : juce::TextButton (text) { setWantsKeyboardFocus (true); }
        presentation::Context context = presentation::defaultContext();
        void paintButton (juce::Graphics& g, bool hover, bool down) override
        {
            const key_light::Scope light (*this);
            surface_material::paintControl (g, getLocalBounds().toFloat().reduced (1), hover, down, false, COL_FLORA_BR, 3);
            g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
            g.setColour (isEnabled() ? COL_NORMAL : COL_MUTED);
            text_style::drawText (g, getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    };
public:
    using Request = reference_audition::TargetRequest;
    TargetBar()
    {
        setComponentID ("reference-targets");
        make.setComponentID ("reference-make-target"); remove.setComponentID ("reference-remove-target");
        select.setComponentID ("reference-target-choice"); details.setComponentID ("reference-target-details");
        details.onClick = [this] { expanded = ! expanded; syncExpanded(); resized(); if (onLayoutChange) onLayoutChange(); };
        make.onClick = [this] { send ({ state.measuring ? Request::Action::cancel : Request::Action::make, {}, {} }); };
        remove.onClick = [this] { send ({ Request::Action::remove, {}, state.collection.selected }); };
        select.onChange = [this] {
            const int at = select.getSelectedId() - 2;
            send ({ Request::Action::select, {}, at >= 0 && at < static_cast<int> (state.collection.items.size())
                ? state.collection.items[static_cast<size_t> (at)].id : juce::String {}, state.restoreEpoch });
        };
        addAndMakeVisible (details); addChildComponent (make); addChildComponent (select); addChildComponent (remove);
    }
    std::function<void(const Request&)> onRequest;
    std::function<void()> onLayoutChange;
    void update (reference_audition::TargetSnapshot next, presentation::Context context, bool canMake, bool compact)
    {
        juce::ignoreUnused (compact);
        compactView = context.referenceCompact;
        state = std::move (next); currentContext = context;
        make.context = remove.context = details.context = context;
        make.setButtonText (state.measuring ? "CANCEL" : "MAKE TARGET");
        constexpr auto capacity = static_cast<int> (reference_audition::TargetCollection::capacity);
        const bool full = state.collection.items.size() >= reference_audition::TargetCollection::capacity;
        unavailable::set (make, state.measuring || (canMake && ! full), ! canMake ? juce::String ("Play A for 3 s to make a target")
                                                                                  : juce::String (capacity) + " targets: remove one first");
        unavailable::set (remove, state.collection.selected.isNotEmpty(), "Choose a target to remove");
        select.clear (juce::dontSendNotification);
        std::vector<SelectionControl::Row> rows;
        const auto prefix = juce::String (state.restoreEpoch) + "/";
        if (! state.collection.items.empty()) { select.addItem ("B / SONG", 1); rows.push_back ({ prefix }); }
        int id = 2, chosen = 0;
        for (const auto& target : state.collection.items)
        {
            select.addItem (target.name, id); rows.push_back ({ prefix + target.id });
            if (target.id == state.collection.selected) chosen = id;
            ++id;
        }
        select.setRows (std::move (rows));
        select.setTextWhenNothingSelected ("CHOOSE TARGET");
        select.setSelectedId (chosen, juce::dontSendNotification);
        syncExpanded();
        resized(); repaint();
    }
    int rowHeight() const noexcept { return expanded ? (compactView ? 40 : 52) : 26; }
    bool isExpanded() const noexcept { return expanded; }
    void resized() override
    {
        auto area = getLocalBounds();
        auto top = area.removeFromTop (compactView && expanded ? 18 : 24);
        details.setBounds (top.removeFromRight (juce::jmin (76, top.getWidth() / 3)));
        if (expanded)
        {
            auto row = area.removeFromTop (compactView ? 18 : 24);
            make.setBounds (row.removeFromLeft (juce::jmin (104, row.getWidth() / 2))); row.removeFromLeft (4);
            remove.setBounds (row.removeFromRight (24)); row.removeFromRight (4);
            select.setBounds (row);
        }
    }
    void paint (juce::Graphics& g) override
    {
        const key_light::Scope light (*this);
        if (compactView && ! expanded) return;
        surface_material::paintPanel (g, getLocalBounds().toFloat(), 0.8f);
        g.setColour (COL_NORMAL);
        g.setFont (labelFont (currentContext, typography::TextRole::unit, typography::Composition::information));
        const auto count = juce::String (static_cast<int> (state.collection.items.size()));
        const auto label = compactView ? "DROP AUDIO / TARGETS " + count + "/16"
                                       : "DROP AUDIO  /  TARGETS " + count + " / 16";
        const auto line = getLocalBounds().removeFromTop (compactView && expanded ? 18 : 24).withTrimmedLeft (6).withRight (details.getX() - 4);
        text_style::drawEllipsized (g, label, line, juce::Justification::centredLeft);
    }
private:
    reference_audition::TargetSnapshot state;
    presentation::Context currentContext = presentation::defaultContext();
    bool expanded = false, compactView = false;
    Button make { "MAKE TARGET" }, remove { "-" }, details { "DETAILS" };
    SelectionControl select;
    void syncExpanded()
    {
        make.setVisible (expanded); select.setVisible (expanded); remove.setVisible (expanded);
        details.setButtonText (expanded ? "CLOSE" : "DETAILS");
    }
    void send (const Request& request) { if (onRequest) onRequest (request); }
};
}
