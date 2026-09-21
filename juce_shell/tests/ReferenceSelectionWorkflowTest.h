#pragma once
#include "../src/HyphaReferenceComponent.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void verifyReferenceSelectionWorkflow (reference_ui::State state)
{
    const auto require = [] (bool ok, const char* message)
    { if (! ok) { std::cerr << "Reference selection: " << message << '\n'; std::exit (EXIT_FAILURE); } };
    reference_ui::Component component;
    state.separateComparisons = true;
    state.versions = { { "v1", "Version 1" }, { "v2", "Version 2" } };
    state.versionId = "v1";
    auto* version = dynamic_cast<juce::ComboBox*> (component.findChildWithID ("reference-version"));
    auto* check = dynamic_cast<juce::ComboBox*> (component.findChildWithID ("reference-check"));
    auto* readout = dynamic_cast<juce::Label*> (component.findChildWithID ("reference-selection-value-1"));
    require (version && check && readout, "selectors and read-only value are owned by one component");
    for (const auto preset : observatory::sizePresets)
    {
        component.setPresentationContext (presentation::forEditor (preset.width, preset.height));
        component.setSize (preset.width, preset.height);
        state.comparisonSlot = 1; component.setState (state);
        const auto b = version->getBounds(), c = check->getBounds();
        state.comparisonSlot = 2; component.setState (state);
        require (version->getBounds() == b && check->getBounds() == c,
                 "B/C dropdowns do not move when Check details appear");
        const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
        if (directory.isNotEmpty())
        {
            juce::Image preview (juce::Image::ARGB, preset.width, preset.height, true);
            juce::Graphics graphics (preview); component.paintEntireComponent (graphics, true);
            auto output = juce::File (directory).getChildFile ("reference-abc-" + juce::String (preset.width) + ".png").createOutputStream();
            require (output && output->setPosition (0) && output->truncate().wasOk()
                && juce::PNGImageFormat().writeImageToStream (preview, *output), "ABC preview written");
        }
    }
    state.versions.resize (1); component.setState (state);
    require (! version->isVisible() && readout->isVisible() && readout->getText() == "Version 1",
             "a known singleton is readable without a disabled input border");
    state.versionId = "not-in-library"; component.setState (state);
    require (version->isVisible() && version->isEnabled() && ! readout->isVisible()
        && version->getSelectedId() == 0, "missing stable ID requires an explicit selection, even with one option");
    state.versionId = "v1"; state.blindPhase = reference_ui::BlindPhase::active; component.setState (state);
    require (! readout->isVisible() && ! version->isVisible() && ! check->isVisible(),
             "read-only labels cannot disclose sources in Blind");
}
}
