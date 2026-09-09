#pragma once

#include "../src/HyphaReferenceAccessPanel.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void verifyReferenceAccessPanelContract()
{
    const auto require = [] (bool value, const char* message)
    {
        if (! value) { std::cerr << "Reference access: " << message << '\n'; std::exit (1); }
    };
    using namespace reference_ui;
    State state;
    require (needsAccessPanel (state), "unconfirmed entitlement has discovery entry");
    for (auto access : { os_access::State::unowned, os_access::State::ownedDisconnected })
    {
        state.osAccess = access;
        require (needsAccessPanel (state) && ! canSelectB (state), "help is not B permission");
        for (auto phase : { BlindPhase::starting, BlindPhase::active,
                            BlindPhase::revealed, BlindPhase::invalidated })
        {
            state.blindPhase = phase;
            require (! needsAccessPanel (state), "return-level UI must stay reachable");
        }
        state.blindPhase = BlindPhase::unavailable;
    }
    for (auto access : { os_access::State::connectedUnprepared, os_access::State::ready })
    {
        state.osAccess = access;
        require (! needsAccessPanel (state), "connected state keeps actual preparation UI");
    }
    for (int width = 300; width <= 900; width += 15)
    {
        observatory::View view (observatory::Role::post);
        view.setSize (width, width * 2 / 3);
        view.setDomain (observatory::Domain::reference);
        AccessPanel panel;
        panel.setBounds (view.bodyBounds());
        int aboutCount = 0, recheckCount = 0;
        panel.onAbout = [&] { ++aboutCount; };
        panel.onRecheck = [&] { ++recheckCount; };
        const auto button = [&] (const char* id)
        {
            auto* result = dynamic_cast<juce::Button*> (panel.findChildWithID (id));
            require (result != nullptr, "action component exists");
            return result;
        };
        const auto verifyLayout = [&]
        {
            for (auto* child : panel.getChildren())
            {
                if (! child->isVisible()) continue;
                require (panel.getLocalBounds().contains (child->getBounds()), "inside body");
                for (auto* other : panel.getChildren())
                    if (other != child && other->isVisible())
                        require (! child->getBounds().intersects (other->getBounds()), "no overlap");
                if (auto* b = dynamic_cast<juce::Button*> (child))
                {
                    require (b->isEnabled() && b->getWantsKeyboardFocus() && b->getHeight() >= 28,
                             "enabled / keyboard / hit target");
                    require (b->findColour (juce::TextButton::textColourOffId) == COL_NORMAL,
                             "active action labels must not look disabled");
                    require (labelFont (b->getHeight() >= 28 ? 13 : 11).getStringWidthFloat (b->getButtonText())
                                 <= b->getWidth() - 6, "small text fits without compression");
                }
                if (auto* label = dynamic_cast<juce::Label*> (child))
                {
                    require (label->getFont().getHeight() >= 12.0f, "font floor");
                    require (label->getMinimumHorizontalScale() == 1.0f, "no horizontal compression");
                    juce::AttributedString text;
                    text.append (label->getText(), label->getFont(), COL_NORMAL);
                    juce::TextLayout layout;
                    layout.createLayout (text, static_cast<float> (label->getWidth() - 10));
                    require (layout.getHeight() <= label->getHeight(), "wrapped text fits in body");
                }
            }
        };
        verifyLayout();
        require (aboutCount == 0 && recheckCount == 0, "opening and layout are passive");
        button ("reference-access-about")->onClick();
        require (aboutCount == 1 && recheckCount == 0, "about is explicit");
        button ("reference-access-owner")->onClick();
        panel.setRecheckUnconfirmed (true);
        verifyLayout();
        require (! button ("reference-access-about")->isVisible(), "owner help is not repurchase");
        require (aboutCount == 1 && recheckCount == 0, "help does not activate or connect");
        button ("reference-access-recheck")->onClick();
        require (recheckCount == 1, "local recheck is explicit");
        panel.setOwned (true);
        require (! panel.getDescription().contains ("License not confirmed"),
                 "external entitlement recognition clears stale recheck failure");
        require (panel.getDescription().contains ("Kirin OS license confirmed"),
                 "recognized owner sees explicit license confirmation");
        require (! panel.getDescription().contains ("activate Kirin OS"),
                 "recognized owner is not told to activate again");
        verifyLayout();
        require (! button ("reference-access-about")->isVisible(), "recognized owner sees no sales CTA");
        panel.setOwned (false);
        verifyLayout();
        require (button ("reference-access-about")->isVisible(), "revocation restores discovery only");
        if (width == 300)
        {
            const auto path = juce::SystemStats::getEnvironmentVariable ("HYPHA_REFERENCE_ACCESS_PNG", {});
            if (path.isNotEmpty())
            {
                juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
                juce::Graphics graphics (image);
                graphics.fillAll (BG);
                panel.paintEntireComponent (graphics, true);
                juce::FileOutputStream stream { juce::File (path) };
                require (stream.openedOk() && stream.setPosition (0)
                             && stream.truncate().wasOk(), "replace previous diagnostic image");
                require (juce::PNGImageFormat().writeImageToStream (image, stream), "diagnostic image");
            }
        }
    }
}
}
