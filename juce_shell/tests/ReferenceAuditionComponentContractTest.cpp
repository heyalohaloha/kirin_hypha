#include "ReferenceAuditionComponentContractTest.h"

#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaReferenceComponent.h"

#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Reference UI contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_REF_REQUIRE(expression) require ((expression), #expression, __LINE__)

juce::Image render (reference_ui::Component& component)
{
    juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics graphics (image);
    component.paintEntireComponent (graphics, true);
    return image;
}

int differentPixels (const juce::Image& left, const juce::Image& right)
{
    KIRIN_REF_REQUIRE (left.getBounds() == right.getBounds());
    int different = 0;
    for (int y = 0; y < left.getHeight(); ++y)
        for (int x = 0; x < left.getWidth(); ++x)
            different += left.getPixelAt (x, y).getARGB()
                      != right.getPixelAt (x, y).getARGB();
    return different;
}

reference_ui::State readyState()
{
    reference_ui::State state;
    state.readiness = reference_ui::Readiness::ready;
    state.title = "Mix v4";
    state.sourceLabel = "WORK VERSION";
    state.status = "READY / B FOLLOWS A";
    state.alignmentLabel = "PROJECT TIMELINE";
    state.aAvailable = true;
    state.aIntegratedLoudness = -14.0;
    state.aMaximumTruePeakDbtp = -1.8;
    return state;
}
}

void verifyReferenceAuditionComponentContract()
{
    auto state = readyState();
    KIRIN_REF_REQUIRE (reference_ui::canSelectB (state));
    state.readiness = reference_ui::Readiness::waiting;
    KIRIN_REF_REQUIRE (! reference_ui::canSelectB (state));
    state = readyState();
    state.aAvailable = false;
    KIRIN_REF_REQUIRE (! reference_ui::canSelectB (state));
    state = readyState();
    state.aIntegratedLoudness = reference_ui::unavailableValue();
    KIRIN_REF_REQUIRE (! reference_ui::canSelectB (state));
    state = readyState();
    state.aMaximumTruePeakDbtp = reference_ui::unavailableValue();
    KIRIN_REF_REQUIRE (! reference_ui::canSelectB (state));

    reference_ui::Component component;
    component.setSize (288, 136);
    component.setState (readyState());
    KIRIN_REF_REQUIRE (! component.detailedLayout());
    const auto compactA = render (component);

    bool requestedA = false;
    bool requestedB = false;
    component.onSelectA = [&requestedA] { requestedA = true; };
    component.onSelectB = [&requestedB] { requestedB = true; };
    auto* a = dynamic_cast<juce::TextButton*> (component.findChildWithID ("reference-a"));
    auto* b = dynamic_cast<juce::TextButton*> (component.findChildWithID ("reference-b"));
    KIRIN_REF_REQUIRE (a != nullptr && b != nullptr && b->isEnabled());
    a->onClick();
    b->onClick();
    KIRIN_REF_REQUIRE (requestedA && requestedB);

    auto selected = readyState();
    selected.bSelected = true;
    selected.status = "B AUDITION / PRE DELTA PAUSED";
    selected.adjustedBIntegratedLoudness = -14.0;
    selected.adjustedBMaximumTruePeakDbtp = -1.0;
    selected.loudnessDeltaBMinusA = 0.0;
    selected.truePeakDeltaBMinusA = 0.8;
    selected.appliedGainDb = 2.0;
    selected.gainLimited = true;
    component.setState (selected);
    const auto compactB = render (component);
    KIRIN_REF_REQUIRE (differentPixels (compactA, compactB) > 100);
    const auto compactOutput = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_REFERENCE_UI_COMPACT_OUTPUT", {});
    if (compactOutput.isNotEmpty())
    {
        juce::FileOutputStream stream { juce::File { compactOutput } };
        KIRIN_REF_REQUIRE (juce::PNGImageFormat().writeImageToStream (compactB, stream));
    }

    component.setSize (888, 470);
    KIRIN_REF_REQUIRE (component.detailedLayout());
    const auto detailed = render (component);
    KIRIN_REF_REQUIRE (detailed.getPixelAt (
        detailed.getWidth() / 4, detailed.getHeight() / 2).getAlpha() != 0);
    const auto outputPath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_REFERENCE_UI_OUTPUT", {});
    if (outputPath.isNotEmpty())
    {
        juce::FileOutputStream stream { juce::File { outputPath } };
        KIRIN_REF_REQUIRE (juce::PNGImageFormat().writeImageToStream (detailed, stream));
    }

    const auto compositePath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_REFERENCE_UI_COMPOSITE_OUTPUT", {});
    if (compositePath.isNotEmpty())
    {
        juce::Component surface;
        observatory::View shell { observatory::Role::post };
        surface.setSize (900, 600);
        shell.setBounds (surface.getLocalBounds());
        shell.setDomain (observatory::Domain::reference);
        shell.setConnection ("PAIR MIX", COL_LED_BLUE, observatory::ConnectionState::paired);
        surface.addAndMakeVisible (shell);
        component.setBounds (shell.bodyBounds());
        surface.addAndMakeVisible (component);
        juce::Image composite (juce::Image::ARGB, 900, 600, true);
        juce::Graphics graphics (composite);
        surface.paintEntireComponent (graphics, true);
        juce::FileOutputStream stream { juce::File { compositePath } };
        KIRIN_REF_REQUIRE (juce::PNGImageFormat().writeImageToStream (composite, stream));
    }
}
}
