#pragma once

#include "ReferenceGuideContractTest.h"
#include "ReferenceControlLookup.h"
#include "../src/HyphaUnavailableReason.h"

namespace hypha::tests
{
inline void verifyReferenceBlindReason()
{
    using reference_guide_contract::require;
    auto state = reference_guide_contract::named ("ready");
    state.comparisonSlot = 1;
    state.blindPhase = reference_ui::BlindPhase::unavailable;
    state.versionReady = state.versionArmable = false;
    state.versionStep = reference_ui::SourceStep::noMatchingPassage;
    for (const auto size : observatory::sizePresets)
    {
        state.blindLargeScreen = size.width >= 900;
        reference_ui::Component panel;
        panel.setPresentationContext (presentation::forEditor (size.width, size.height));
        panel.setSize (size.width - 12, size.width == 900 ? 470 : size.height - 64);
        panel.setState (state);
        if (! state.blindLargeScreen)
        {
            auto* details = dynamic_cast<juce::Button*> (findReferenceControl (panel, "reference-check-details"));
            require (details != nullptr && details->isVisible(), "compact V offers details");
            details->onClick();
        }
        auto* blind = dynamic_cast<juce::Button*> (findReferenceControl (panel, "reference-blind"));
        require (blind != nullptr && blind->isVisible() && ! blind->isEnabled(), "unavailable Version Blind stays visible and disabled");
        require (unavailable::reasonAt (blind) == reference_ui::unavailableText (state, true)
                     && unavailable::reasonAt (blind).isNotEmpty(), "disabled Version Blind keeps its reason at every size");
    }
}
}
