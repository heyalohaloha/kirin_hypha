#pragma once

#include "../src/HyphaMainFrame.h"
#include "../src/HyphaObservatoryView.h"
#include "ReferenceGuideStates.h"
#include "ReferenceInteriorLightContract.h"
#include "ReferenceStateLightContract.h"
#include "ReferenceComparisonLayoutContract.h"

#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
namespace reference_light_contract
{
inline void require (bool ok, const juce::String& message)
{
    if (ok) return;
    std::cerr << "Reference light contract failed: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

inline reference_ui::State named (const char* name)
{
    for (const auto& item : reference_review::cases())
        if (juce::String (item.name) == name) return item.state;
    require (false, juce::String ("missing fixture ") + name);
    return {};
}

// Sample the actual rendered bronze ring outside the child/chart area. This catches a missing
// frame on parent-painted C views, and a frame painted using another component's light origin.
inline void requireRenderedFrame (reference_ui::Component& panel, juce::Rectangle<int> window,
                                  const juce::String& where)
{
    require (! window.isEmpty() && panel.getLocalBounds().contains (window), where + ": window fits");
    juce::Image actual (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
    juce::Image expected (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
    juce::Graphics rendered (actual), isolated (expected);
    rendered.fillAll (juce::Colours::black);
    isolated.fillAll (juce::Colours::black);
    panel.paintEntireComponent (rendered, true);
    const key_light::Scope light (panel);
    main_frame::paint (isolated, window.toFloat());
    const auto y = window.getY() - 2;
    require (y >= 0, where + ": frame has room above the glass");
    int painted = 0;
    for (int x = window.getX() + 10; x < window.getRight() - 10; x += 7)
    {
        const auto got = actual.getPixelAt (x, y);
        const auto want = expected.getPixelAt (x, y);
        require (std::abs (got.getRed() - want.getRed()) <= 2
                     && std::abs (got.getGreen() - want.getGreen()) <= 2
                     && std::abs (got.getBlue() - want.getBlue()) <= 2,
                 where + ": the common observation catches its own editor light");
        painted += want != juce::Colours::black;
    }
    require (painted > 0, where + ": a bronze frame is actually painted");
}

inline void requireControlsOutside (reference_ui::Component& panel, juce::Rectangle<int> window,
                                    const juce::String& where)
{
    for (const auto* id : { "reference-a", "reference-b", "reference-c", "reference-version",
                            "reference-check", "reference-preset", "reference-cue",
                            "reference-visual-slot", "reference-action" })
        if (const auto* control = panel.findChildWithID (id); control && control->isVisible())
            require (! window.intersects (control->getBounds()),
                     where + ": observation leaves " + id + " reachable");
}

inline void writeConfiguredViewIfRequested (reference_ui::Component& panel, int editorWidth)
{
    const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_LIGHT_OUTPUT", {});
    if (output.isEmpty()) return;
    const juce::File directory (output);
    require (directory.createDirectory().wasOk(), "configured Reference screenshot directory");
    // The existing fixture has visibly distinct A/C spectrum curves and keeps B audible while
    // C is inspected, exercising the current ABCV controls as well as the common chart frame.
    panel.setState (named ("b_audible_c_view"));
    juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
    juce::Graphics graphics (image);
    graphics.fillAll (juce::Colours::black);
    panel.paintEntireComponent (graphics, true);
    juce::FileOutputStream stream (directory.getChildFile (
        "reference-C-configured-" + juce::String (editorWidth) + ".png"));
    require (stream.setPosition (0) && stream.truncate().wasOk()
                 && juce::PNGImageFormat().writeImageToStream (image, stream),
             "configured Reference screenshot at " + juce::String (editorWidth));
}

inline juce::Image renderOperation (juce::Button& button)
{
    juce::Image image (juce::Image::ARGB, button.getWidth(), button.getHeight(), true);
    juce::Graphics graphics (image);
    graphics.fillAll (juce::Colours::black);
    button.paintEntireComponent (graphics, true);
    return image;
}

inline int bevelBrightness (const juce::Image& image, bool right)
{
    int brightness = 0;
    const auto begin = right ? image.getWidth() * 2 / 3 : 6;
    const auto end = right ? image.getWidth() - 6 : image.getWidth() / 3;
    for (int y = 1; y <= 3; ++y)
        for (int x = begin; x < end; ++x)
        {
            const auto colour = image.getPixelAt (x, y);
            brightness += colour.getRed() + colour.getGreen() + colour.getBlue();
        }
    return brightness;
}

inline void requireLitOperation (juce::Component& family, juce::Button& button,
                                 const juce::String& name)
{
    const auto bounds = button.getBounds();
    const auto y = family.getY();
    family.setTopLeftPosition (20, y);
    const auto left = renderOperation (button);
    family.setTopLeftPosition (650, y);
    const auto right = renderOperation (button);
    require (bevelBrightness (left, true) > bevelBrightness (right, true)
                 && bevelBrightness (right, false) > bevelBrightness (left, false),
             name + ": moving past the editor light changes which bevel catches it");
    const auto different = [&] (const juce::Image& first, const juce::Image& second)
    {
        int pixels = 0;
        for (int py = 0; py < first.getHeight(); ++py)
            for (int px = 0; px < first.getWidth(); ++px)
                pixels += first.getPixelAt (px, py) != second.getPixelAt (px, py);
        return pixels;
    };
    button.setState (juce::Button::buttonOver);
    const auto hovered = renderOperation (button);
    button.setState (juce::Button::buttonDown);
    const auto pressed = renderOperation (button);
    button.setState (juce::Button::buttonNormal);
    button.setToggleState (true, juce::dontSendNotification);
    const auto selected = renderOperation (button);
    require (different (right, hovered) > 10 && different (hovered, pressed) > 10
                 && different (right, selected) > 10,
             name + ": hover, press and selection remain visibly distinct");
    button.setToggleState (false, juce::dontSendNotification);
    button.setEnabled (false);
    const auto disabled = renderOperation (button);
    button.setState (juce::Button::buttonOver);
    const auto disabledHover = renderOperation (button);
    for (int py = 1; py <= 3; ++py)
        for (int px = 6; px < button.getWidth() - 6; ++px)
            require (disabled.getPixelAt (px, py) == disabledHover.getPixelAt (px, py),
                     name + ": a disabled command cannot brighten on hover");
    require (button.getBounds() == bounds, name + ": lighting and states preserve command bounds");
}

inline void verifyOperationFamilies()
{
    juce::Component root;
    root.getProperties().set (key_light::rootProperty, true);
    root.setSize (900, 600);
    const auto context = presentation::forEditor (900, 600);
    reference_ui::CaptureControls capture;
    root.addAndMakeVisible (capture);
    capture.setBounds (0, 180, 180, 24);
    capture.update (std::make_shared<reference_audition::ACaptureAccess>(), false, context);
    auto* action = dynamic_cast<juce::Button*> (capture.findChildWithID ("capture-a-action"));
    require (action && action->isVisible(), "Capture action exists");
    requireLitOperation (capture, *action, "Capture");

    reference_ui::ComparisonView comparison;
    root.addAndMakeVisible (comparison);
    comparison.setBounds (0, 240, 180, 160);
    comparison.update ({}, -1, context, false);
    auto* follow = dynamic_cast<juce::Button*> (comparison.findChildWithID ("reference-follow"));
    require (follow && follow->isVisible(), "Comparison Follow command exists");
    requireLitOperation (comparison, *follow, "Comparison");

    reference_ui::ReferenceSelectorLookAndFeel look;
    look.setPresentationContext (context);
    reference_ui::WorkflowControls workflow;
    workflow.setLookAndFeel (&look);
    root.addAndMakeVisible (workflow);
    workflow.setBounds (0, 420, 180, 24);
    reference_audition::WorkflowView view;
    view.reviewAvailable = true;
    workflow.update (view, false, false);
    auto* today = dynamic_cast<juce::Button*> (workflow.getChildComponent (0));
    require (today && today->isVisible(), "Workflow Today command exists");
    requireLitOperation (workflow, *today, "Workflow");
    workflow.setLookAndFeel (nullptr);
}
}

inline void verifyReferenceLightContract()
{
    using namespace reference_light_contract;
    for (const auto& preset : observatory::sizePresets)
    {
        observatory::View shell (observatory::Role::post);
        shell.setSize (preset.width, preset.height);
        shell.setDomain (observatory::Domain::reference);
        shell.setExternalAnalysisBodyActive (true);
        juce::Component root;
        root.getProperties().set (key_light::rootProperty, true);
        root.setSize (preset.width, preset.height);
        reference_ui::Component panel;
        root.addAndMakeVisible (panel);
        panel.setPresentationContext (presentation::forEditor (preset.width, preset.height));
        panel.setBounds (shell.analysisBodyBounds());
        const auto size = " at " + juce::String (preset.width);
        auto state = named ("ready");
        state.comparisonSlot = 1;
        panel.setState (state);
        const auto* comparison = panel.findChildWithID ("reference-comparison-view");
        const auto* tonal = panel.findChildWithID ("reference-tonal-view");
        require (comparison && tonal, "Reference observation children exist");
        require (comparison->isVisible() && !tonal->isVisible()
                     && panel.observationWindowBounds() == comparison->getBounds(),
                 "B frames its actual comparison window" + size);
        requireRenderedFrame (panel, panel.observationWindowBounds(), "B" + size);
        requireControlsOutside (panel, panel.observationWindowBounds(), "B" + size);

        state.comparisonSlot = 2;
        state.viewBindings = { "balance" };
        panel.setState (state);
        require (tonal->isVisible() && !comparison->isVisible()
                     && panel.observationWindowBounds() == tonal->getBounds(),
                 "C Balance frames its actual tonal window" + size);
        requireRenderedFrame (panel, panel.observationWindowBounds(), "C Balance" + size);

        for (const auto* layout : { "equal", "main" })
            for (int count = 1; count <= 3; ++count)
            {
                state.viewBindings = { "dynamics", "loudness", "spectrum_full" };
                state.viewBindings.resize (static_cast<size_t> (count));
                state.presentationLayout = layout;
                panel.setState (state);
                const auto where = "C configured " + juce::String (count) + " / " + layout + size;
                require (!comparison->isVisible() && !tonal->isVisible(),
                         where + ": configured charts paint in the parent");
                const auto window = panel.observationWindowBounds();
                if (observatory::isFullDensity (preset.density))
                {
                    requireRenderedFrame (panel, window, where);
                    requireControlsOutside (panel, window, where);
                }
                else
                    require (window.isEmpty(), where + ": compact metric cards stay quiet");
            }

        state.viewBindings.clear();
        panel.setState (state);
        require (panel.observationWindowBounds().isEmpty(), "fallback metric cards stay quiet" + size);
        for (const auto* name : { "stopped", "no_check", "no_library" })
        {
            state = named (name);
            state.viewBindings = { "balance", "spectrum_full" };
            panel.setState (state);
            require (reference_ui::guide (state).shown && panel.observationWindowBounds().isEmpty()
                         && !comparison->isVisible() && !tonal->isVisible(),
                     juce::String (name) + ": guide-only page stays quiet" + size);
        }
        for (const auto phase : { reference_ui::BlindPhase::starting, reference_ui::BlindPhase::active,
                                  reference_ui::BlindPhase::revealed, reference_ui::BlindPhase::invalidated })
        {
            state = named ("ready");
            state.comparisonSlot = 1;
            state.viewBindings = { "balance", "spectrum_full" };
            state.blindPhase = phase;
            panel.setState (state);
            require (panel.observationWindowBounds().isEmpty()
                         && !comparison->isVisible() && !tonal->isVisible(),
                     "Blind states never frame an observation" + size);
        }
        writeConfiguredViewIfRequested (panel, preset.width);
    }
    verifyOperationFamilies();
    reference_interior_light::verify();
    reference_state_light::verify();
    reference_comparison_layout::verify();
}
}
