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

void writeImageIfRequested (const juce::Image& image, const char* variable)
{
    const auto path = juce::SystemStats::getEnvironmentVariable (variable, {});
    if (path.isEmpty())
        return;
    juce::FileOutputStream stream { juce::File { path } };
    KIRIN_REF_REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
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
    state.osAccess = os_access::State::ready;
    state.auditionBuffered = true;
    state.blindPhase = reference_ui::BlindPhase::available;
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
    KIRIN_REF_REQUIRE (reference_ui::canStartBlind (state));
    state.readiness = reference_ui::Readiness::waiting;
    KIRIN_REF_REQUIRE (! reference_ui::canSelectB (state));
    KIRIN_REF_REQUIRE (! reference_ui::canStartBlind (state));
    state = readyState();
    state.aAvailable = false;
    KIRIN_REF_REQUIRE (! reference_ui::canSelectB (state));
    state = readyState();
    state.osAccess = os_access::State::unowned;
    KIRIN_REF_REQUIRE (! reference_ui::canSelectB (state));
    state.osAccess = os_access::State::ownedDisconnected;
    KIRIN_REF_REQUIRE (! reference_ui::canSelectB (state));
    state.osAccess = os_access::State::connectedUnprepared;
    KIRIN_REF_REQUIRE (! reference_ui::canSelectB (state));
    state = readyState();
    state.auditionBuffered = false;
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
    bool requestedBlind = false;
    bool requestedReveal = false;
    bool requestedEnd = false;
    int requestedStimulus = 0;
    component.onSelectA = [&requestedA] { requestedA = true; };
    component.onSelectB = [&requestedB] { requestedB = true; };
    component.onStartBlind = [&requestedBlind] { requestedBlind = true; };
    component.onSelectBlindStimulus = [&requestedStimulus] (int value) {
        requestedStimulus = value;
    };
    component.onRevealBlind = [&requestedReveal] { requestedReveal = true; };
    component.onEndBlind = [&requestedEnd] { requestedEnd = true; };
    auto* a = dynamic_cast<juce::TextButton*> (component.findChildWithID ("reference-a"));
    auto* b = dynamic_cast<juce::TextButton*> (component.findChildWithID ("reference-b"));
    auto* startBlind = dynamic_cast<juce::TextButton*> (
        component.findChildWithID ("reference-blind"));
    auto* one = dynamic_cast<juce::TextButton*> (
        component.findChildWithID ("reference-blind-1"));
    auto* two = dynamic_cast<juce::TextButton*> (
        component.findChildWithID ("reference-blind-2"));
    auto* reveal = dynamic_cast<juce::TextButton*> (
        component.findChildWithID ("reference-blind-reveal"));
    auto* endBlind = dynamic_cast<juce::TextButton*> (
        component.findChildWithID ("reference-blind-end"));
    KIRIN_REF_REQUIRE (a != nullptr && b != nullptr && b->isEnabled()
                       && startBlind != nullptr && startBlind->isVisible()
                       && one != nullptr && two != nullptr && reveal != nullptr
                       && endBlind != nullptr);
    auto unavailableBlind = readyState();
    unavailableBlind.blindPhase = reference_ui::BlindPhase::unavailable;
    component.setState (unavailableBlind);
    KIRIN_REF_REQUIRE (! startBlind->isVisible() && a->isVisible() && b->isVisible());
    component.setState (readyState());
    a->onClick();
    b->onClick();
    startBlind->onClick();
    KIRIN_REF_REQUIRE (requestedA && requestedB && requestedBlind);

    auto blindState = readyState();
    blindState.blindPhase = reference_ui::BlindPhase::active;
    blindState.activeBlindStimulus = 1;
    blindState.pendingBlindStimulus = 2;
    component.setState (blindState);
    KIRIN_REF_REQUIRE (! a->isVisible() && ! b->isVisible() && ! startBlind->isVisible()
                       && one->isVisible() && two->isVisible() && reveal->isVisible()
                       && endBlind->isVisible() && one->isEnabled() && ! two->isEnabled());
    blindState.pendingBlindStimulus = 0;
    component.setState (blindState);
    one->onClick();
    two->onClick();
    reveal->onClick();
    endBlind->onClick();
    KIRIN_REF_REQUIRE (requestedStimulus == 2 && requestedReveal && requestedEnd);
    const auto concealedA = render (component);
    writeImageIfRequested (concealedA, "KIRIN_REFERENCE_UI_BLIND_OUTPUT");
    blindState.title = "Identity must not affect blind pixels";
    blindState.sourceLabel = "CATALOG";
    blindState.status = "B AUDITION / PRE DELTA PAUSED";
    blindState.alignmentLabel = "REFERENCE CUE";
    blindState.aIntegratedLoudness = -3.0;
    blindState.aMaximumTruePeakDbtp = 1.5;
    blindState.adjustedBIntegratedLoudness = -28.0;
    blindState.adjustedBMaximumTruePeakDbtp = -12.0;
    blindState.loudnessDeltaBMinusA = 25.0;
    blindState.truePeakDeltaBMinusA = 13.5;
    blindState.appliedGainDb = -14.0;
    blindState.bSelected = true;
    component.setState (blindState);
    const auto concealedB = render (component);
    KIRIN_REF_REQUIRE (differentPixels (concealedA, concealedB) == 0);

    blindState.blindPhase = reference_ui::BlindPhase::revealed;
    blindState.blindReveal = "1 = B  /  2 = A";
    component.setState (blindState);
    const auto revealed = render (component);
    writeImageIfRequested (revealed, "KIRIN_REFERENCE_UI_REVEALED_OUTPUT");
    KIRIN_REF_REQUIRE (! reveal->isVisible()
                       && differentPixels (concealedB, revealed) > 100);

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
    writeImageIfRequested (compactB, "KIRIN_REFERENCE_UI_COMPACT_OUTPUT");

    component.setSize (888, 470);
    KIRIN_REF_REQUIRE (component.detailedLayout());
    const auto detailed = render (component);
    KIRIN_REF_REQUIRE (detailed.getPixelAt (
        detailed.getWidth() / 4, detailed.getHeight() / 2).getAlpha() != 0);
    writeImageIfRequested (detailed, "KIRIN_REFERENCE_UI_OUTPUT");

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
