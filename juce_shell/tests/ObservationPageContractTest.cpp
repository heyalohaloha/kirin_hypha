#include "ObservationPageContractTest.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTimePageNavigation.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
namespace
{
void require (bool value, const juce::String& message)
{
    if (value) return;
    std::cerr << "Observation page contract: " << message << '\n';
    std::exit (EXIT_FAILURE);
}
void verifyButtons (juce::Component& component)
{
    for (auto* child : component.getChildren())
    {
        if (! child->isVisible()) continue;
        require (component.getLocalBounds().contains (child->getBounds()), "control outside parent");
        if (auto* button = dynamic_cast<observatory::Button*> (child))
        {
            const auto height = button->getHeight();
            const auto font = labelFont (
                presentation::forEditor (component.getWidth(), component.getHeight()),
                typography::TextRole::action);
            require (font.getStringWidthFloat (button->getButtonText()) <= button->getWidth() - 6.0f,
                button->getButtonText() + " cannot fit " + juce::String (button->getWidth()) + "px");
            require (height >= font.getHeight() + 2.0f, "text vertically clipped");
        }
    }
}
}

void verifyObservationPageContract()
{
    using namespace observatory;
    using Page = analysis_navigation::Page;
    for (auto role : { Role::pre, Role::post })
        for (const auto preset : sizePresets)
        {
            View view (role);
            view.setSize (preset.width, preset.height);
            view.setDomain (Domain::time);
            view.setTarget (ObservationTarget::delta);
            for (auto page : { Page::meters, Page::run, Page::attack, Page::perceptual, Page::absolute })
            {
                view.setAnalysisPage (page);
                const auto caps = view.capabilities();
                const auto fixed = role == Role::pre || page != Page::meters;
                require (caps.targetSelectable != fixed, "target capability mismatch");
                if (role == Role::post && (page == Page::attack || page == Page::perceptual
                    || page == Page::absolute))
                    require (! caps.historyRange && ! caps.loudnessScale, "irrelevant TIME controls");
                verifyButtons (view);
                require (view.bodyBounds().contains (view.analysisBodyBounds()), "body outside shell");
                require (view.analysisBodyBounds().getHeight() >= 28, "analysis body collapsed");
                if (role == Role::post)
                {
                    TimePageNavigation tabs;
                    tabs.setPresentationContext (
                        presentation::forEditor (preset.width, preset.height));
                    tabs.setDirect (preset.width >= 450);
                    tabs.setPage (page);
                    tabs.setSize (view.timeNavigationBounds().getWidth(), view.timeNavigationBounds().getHeight());
                    verifyButtons (tabs);
                }
            }
            view.setAnalysisPage (Page::meters);
            require (view.target() == (role == Role::post ? ObservationTarget::delta
                                                        : ObservationTarget::absolute), "target preference lost");
            for (auto domain : { Domain::level, Domain::frequency, Domain::space })
            {
                view.setDomain (domain);
                verifyButtons (view);
            }
        }
}
}
