#pragma once
#include "HyphaReferenceSelectorLookAndFeel.h"
#include "HyphaReferenceSelectionControl.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaKeyLight.h"
#include "HyphaTextStyle.h"
#include "HyphaUnavailableReason.h"
#include "reference_audition/ReferenceTargetTypes.h"

namespace hypha::reference_ui
{
// A page selector with a label surface; it never auditions C.
class TargetBadge final : public juce::TextButton
{
public:
    TargetBadge() : juce::TextButton ("C / DATA") { setWantsKeyboardFocus (true); }
    presentation::Context context = presentation::defaultContext();
    void paintButton (juce::Graphics& g, bool hover, bool) override
    {
        const key_light::Scope light (*this);
        surface_material::paintPanel (g, getLocalBounds().toFloat().reduced (1), 0.65f);
        g.setColour (COL_NORMAL.withAlpha (hover ? 1.0f : 0.8f));
        g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
        text_style::drawText (g, getButtonText(), getLocalBounds(), juce::Justification::centred);
    }
};
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
        make.setComponentID ("reference-make-target");
        remove.setComponentID ("reference-remove-target");
        select.setComponentID ("reference-target-choice");
        select.setTextWhenNothingSelected ("DROP AUDIO");
        select.setColour (juce::ComboBox::backgroundColourId, kFieldFill);
        select.setColour (juce::ComboBox::textColourId, COL_NORMAL);
        select.setColour (juce::ComboBox::outlineColourId, COL_MUTED.withAlpha (0.46f));
        select.setColour (juce::ComboBox::arrowColourId, COL_NORMAL);
        make.onClick = [this] { send ({ state.measuring ? Request::Action::cancel : Request::Action::make, {}, {} }); };
        remove.onClick = [this] { send ({ Request::Action::remove, {}, state.collection.selected }); };
        select.onChange = [this] {
            const int at = select.getSelectedId() - 2;
            send ({ Request::Action::select, {}, at >= 0 && at < static_cast<int> (state.collection.items.size())
                ? state.collection.items[static_cast<size_t> (at)].id : juce::String {}, state.restoreEpoch });
        };
        addAndMakeVisible (make); addAndMakeVisible (select); addAndMakeVisible (remove);
        status.setColour (juce::Label::textColourId, COL_TEXT_SECONDARY);
        status.setMinimumHorizontalScale (1.0f);
        addChildComponent (status);
    }
    std::function<void(const Request&)> onRequest;
    void update (reference_audition::TargetSnapshot next, presentation::Context context, bool canMake, bool compact)
    {
        state = std::move (next);
        make.context = remove.context = context;
        make.setButtonText (state.measuring ? "CANCEL" : "MAKE TARGET");
        constexpr auto capacity = static_cast<int> (reference_audition::TargetCollection::capacity);
        const bool full = state.collection.items.size() >= reference_audition::TargetCollection::capacity;
        unavailable::set (make, state.measuring || (canMake && ! full),
                          ! canMake ? juce::String ("Play A for 3 s to make a target")
                                    : juce::String (capacity) + " targets: remove one first");
        make.setTooltip (state.measuring ? "CANCEL" : canMake ? "MAKE TARGET" : "A WINDOW / 3 S");
        unavailable::set (remove, state.collection.selected.isNotEmpty(), "Choose a target to remove");
        remove.setTooltip ("REMOVE TARGET");
        select.clear (juce::dontSendNotification);
        std::vector<SelectionControl::Row> rows;
        const auto prefix = juce::String (state.restoreEpoch) + "/";
        if (! state.collection.items.empty()) { select.addItem ("C / KIRIN OS", 1); rows.push_back ({ prefix }); }
        int id = 2, chosen = 0;
        for (const auto& target : state.collection.items)
        {
            select.addItem (target.name + " / DATA", id);
            rows.push_back ({ prefix + target.id });
            if (target.id == state.collection.selected) chosen = id;
            ++id;
        }
        select.setRows (std::move (rows));
        select.setTextWhenNothingSelected (state.collection.items.empty() ? juce::String ("DROP AUDIO")
            : "TARGETS " + juce::String (static_cast<int> (state.collection.items.size())) + " / " + juce::String (capacity));
        select.setSelectedId (chosen, juce::dontSendNotification);
        select.setTooltip (state.collection.selected.isNotEmpty() ? "DATA ONLY" : "DROP AUDIO");
        status.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
        status.setText (state.measuring ? "MEASURING " + juce::String (juce::roundToInt (state.progress * 100)) + "%"
                                      : state.issue, juce::dontSendNotification);
        status.setVisible (! compact && (state.measuring || state.issue.isNotEmpty()));
        resized(); repaint();
    }
    int rowHeight() const noexcept { return status.isVisible() ? 48 : 26; }
    void resized() override
    {
        auto area = getLocalBounds();
        if (status.isVisible()) status.setBounds (area.removeFromBottom (22));
        auto row = area.removeFromTop (24);
        make.setBounds (row.removeFromLeft (104)); row.removeFromLeft (4);
        remove.setBounds (row.removeFromRight (24)); row.removeFromRight (4);
        select.setBounds (row);
    }
    void paint (juce::Graphics& g) override
    {
        const key_light::Scope light (*this);
        surface_material::paintPanel (g, getLocalBounds().toFloat(), 0.8f);
        if (state.measuring)
        {
            const auto y = static_cast<float> (getHeight() - 1);
            g.setColour (COL_NORMAL.withAlpha (0.55f));
            g.fillRect (0.0f, y, static_cast<float> (getWidth() * state.progress), 1.0f);
        }
    }
private:
    reference_audition::TargetSnapshot state;
    Button make { "MAKE TARGET" }, remove { "-" };
    SelectionControl select;
    juce::Label status;
    void send (const Request& request) { if (onRequest) onRequest (request); }
};
}
