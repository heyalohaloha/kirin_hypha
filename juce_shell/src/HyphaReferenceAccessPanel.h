#pragma once

#include "HyphaObservatoryView.h"
#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
// Discovery is not audition permission. Never cover an outstanding return-level control.
inline bool needsAccessPanel (const State& state) noexcept
{
    return (state.osAccess == os_access::State::unowned
            || state.osAccess == os_access::State::ownedDisconnected)
        && ! state.bSelected && ! isBlindSession (state.blindPhase);
}

class AccessPanel final : public juce::Component
{
public:
    std::function<void()> onAbout;
    std::function<void()> onRecheck;

    AccessPanel()
    {
        setTitle ("Kirin OS Reference access");
        for (auto* label : { &heading, &detail })
        {
            addAndMakeVisible (*label);
            label->setMinimumHorizontalScale (1.0f);
            label->setJustificationType (juce::Justification::topLeft);
            label->setColour (juce::Label::textColourId, COL_NORMAL);
        }
        heading.setComponentID ("reference-access-heading");
        detail.setComponentID ("reference-access-detail");
        for (auto* button : { &about, &owner, &recheck })
        {
            addAndMakeVisible (*button);
            button->setColour (juce::TextButton::textColourOffId, COL_NORMAL);
            button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        about.setComponentID ("reference-access-about");
        owner.setComponentID ("reference-access-owner");
        recheck.setComponentID ("reference-access-recheck");
        about.setTitle ("About Kirin OS / trial and purchase information");
        owner.setTitle ("Already own Kirin OS / connection help");
        recheck.setTitle ("Recheck the local Kirin OS license");
        about.onClick = [this] { if (onAbout) onAbout(); };
        owner.onClick = [this] { ownerHelp = ! ownerHelp; refresh(); };
        recheck.onClick = [this] { if (onRecheck) onRecheck(); };
        refresh();
    }

    void setPresentationContext (presentation::Context next)
    {
        if (presentationContext == next) return;
        presentationContext = next;
        for (auto* button : { &about, &owner, &recheck })
            button->setPresentationContext (next);
        resized();
        repaint();
    }

    void setOwned (bool value)
    {
        if (owned == value) return;
        owned = value;
        ownerHelp = false;
        unconfirmed = false;
        refresh();
    }

    void setRecheckUnconfirmed (bool value)
    {
        if (unconfirmed == value) return;
        unconfirmed = value;
        refresh();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4);
        const bool large = observatory::isFullDensity (presentationContext.density);
        heading.setFont (labelFont (presentationContext, typography::TextRole::sectionTitle,
                                    typography::Composition::information));
        detail.setFont (labelFont (presentationContext, typography::TextRole::body,
                                   typography::Composition::information));
        heading.setBounds (area.removeFromTop (large ? 24 : 20));
        juce::Rectangle<int> actions;
        if (! owned)
        {
            actions = area.removeFromBottom (large ? 32 : 28);
            area.removeFromBottom (3);
        }
        detail.setBounds (area);
        const int gap = 4;
        if (! owned)
        {
            auto first = actions.removeFromLeft ((actions.getWidth() - gap) / 2);
            actions.removeFromLeft (gap);
            if (ownerHelp) recheck.setBounds (first); else about.setBounds (first);
            owner.setBounds (actions);
        }
    }

private:
    bool owned = false, ownerHelp = false, unconfirmed = false;
    presentation::Context presentationContext = presentation::defaultContext();
    juce::Label heading, detail;
    observatory::Button about { "ABOUT KIRIN OS", false };
    observatory::Button owner { "ALREADY OWN IT?", false };
    observatory::Button recheck { "RECHECK LICENSE", false };

    void refresh()
    {
        const bool help = owned || ownerHelp;
        heading.setText (owned ? "WAITING FOR KIRIN OS" : "REFERENCE / KIRIN OS",
                         juce::dontSendNotification);
        const auto text = owned
            ? juce::String ("Open Kirin OS > INSPECT.\n"
                            "Choose Connect Hypha POST for this saved work.")
            : help
                ? (unconfirmed ? "License not confirmed. " : "")
                    + juce::String ("Open Kirin OS, then Recheck License.\n"
                                    "In INSPECT, choose Connect Hypha POST.")
                : juce::String ("Compare audio registered in Kirin OS.\n"
                                "About: product, trial and purchase information.");
        detail.setText (text, juce::dontSendNotification);
        setDescription (text);
        about.setVisible (! help);
        owner.setVisible (! owned);
        owner.setButtonText (ownerHelp ? "BACK" : "ALREADY OWN IT?");
        recheck.setVisible (! owned && help);
        resized();
    }
};
}
