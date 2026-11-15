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
    state.blindLargeScreen = true;
    state.blindPhase = reference_ui::BlindPhase::unavailable;
    state.versionReady = state.versionArmable = false;
    state.versionStep = reference_ui::SourceStep::noMatchingPassage;
    reference_ui::Component panel;
    panel.setPresentationContext (presentation::forEditor (900, 600));
    panel.setSize (888, 470);
    panel.setState (state);
    auto* blind = dynamic_cast<juce::Button*> (findReferenceControl (panel, "reference-blind"));
    require (blind != nullptr && blind->isVisible() && ! blind->isEnabled(), "unavailable Version Blind stays visible and disabled");
    require (unavailable::reasonAt (blind) == reference_ui::unavailableText (state, true)
                 && unavailable::reasonAt (blind).isNotEmpty(), "disabled Version Blind keeps its reason");
}
}
