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
    KIRIN_REF_REQUIRE (stream.setPosition (0) && stream.truncate().wasOk());
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
    state.presetId = "preset-a";
    state.checkId = "check-a";
    state.candidateId = "reference-a";
    state.cueId = "cue-a";
    state.presetName = "Mix Reference";
    state.checkLabel = "Low End";
    state.candidateName = "Mix v4";
    state.cueLabel = "Full Track";
    state.presets = { { "preset-a", "Mix Reference" }, { "preset-b", "Mastering" } };
    state.checks = { { "check-a", "Low End" }, { "check-b", "Dynamics" } };
    state.candidates = { { "reference-a", "Mix v4" }, { "reference-b", "Mix v3" } };
    state.cues = { { "cue-a", "Full Track" }, { "cue-b", "Chorus" } };
    return state;
}
}

void verifyReferenceAuditionComponentContract()
{
    constexpr auto presentationContext = presentation::forEditor (450, 300);
    KIRIN_REF_REQUIRE (! requiresNativeTextFont ("Mix Reference"));
    KIRIN_REF_REQUIRE (requiresNativeTextFont (juce::String::fromUTF8 ("全工程｜基本5項目")));
    KIRIN_REF_REQUIRE (displayTextFont (juce::String::fromUTF8 ("低域"), presentationContext,
                                       typography::TextRole::selector,
                                       typography::Composition::information)
                           .getTypefaceName()
                       != labelFont (presentationContext, typography::TextRole::selector,
                                     typography::Composition::information).getTypefaceName()
                       || ! usingKimeraTypography());

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
    KIRIN_REF_REQUIRE (reference_ui::canSelectB (state));
    KIRIN_REF_REQUIRE (reference_ui::canStartBlind (state));
    state = readyState();
    state.aMaximumTruePeakDbtp = reference_ui::unavailableValue();
    KIRIN_REF_REQUIRE (reference_ui::canSelectB (state));
    KIRIN_REF_REQUIRE (reference_ui::canStartBlind (state));

    reference_ui::Component component;
    component.setPresentationContext (presentation::forEditor (300, 200));
    component.setSize (288, 136);
    auto compactState = readyState(); compactState.blindLargeScreen = false;
    component.setState (compactState);
    KIRIN_REF_REQUIRE (! component.detailedLayout());
    const auto compactA = render (component);

    bool requestedA = false;
    bool requestedB = false;
    bool requestedBlind = false;
    int requestedAnswer = 0;
    bool requestedReveal = false;
    bool requestedEnd = false;
    bool requestedAction = false;
    int requestedStimulus = 0;
    juce::String requestedPreset;
    juce::String requestedCheck;
    juce::String requestedCandidate;
    juce::String requestedCue;
    component.onSelectA = [&requestedA] { requestedA = true; };
    component.onSelectB = [&requestedB] { requestedB = true; };
    component.onStartBlind = [&requestedBlind] { requestedBlind = true; };
    component.onSelectBlindStimulus = [&requestedStimulus] (int value) {
        requestedStimulus = value;
    };
    component.onAnswerBlind = [&requestedAnswer] (int value) {
        requestedAnswer = value;
    };
    component.onRevealBlind = [&requestedReveal] { requestedReveal = true; };
    component.onEndBlind = [&requestedEnd] { requestedEnd = true; };
    component.onSelectPreset = [&requestedPreset] (const juce::String& id) {
        requestedPreset = id;
    };
    component.onSelectCheck = [&requestedCheck] (const juce::String& id) {
        requestedCheck = id;
    };
    component.onSelectCandidate = [&requestedCandidate] (const juce::String& id) {
        requestedCandidate = id;
    };
    component.onSelectCue = [&requestedCue] (const juce::String& id) {
        requestedCue = id;
    };
    component.onAction = [&requestedAction] { requestedAction = true; };
    auto* a = dynamic_cast<juce::TextButton*> (component.findChildWithID ("reference-a"));
    auto* b = dynamic_cast<juce::TextButton*> (component.findChildWithID ("reference-b"));
    auto* startBlind = dynamic_cast<juce::TextButton*> (
        component.findChildWithID ("reference-blind"));
    auto* compactCandidate = dynamic_cast<juce::ComboBox*> (
        component.findChildWithID ("reference-candidate"));
    auto* compactCheck = dynamic_cast<juce::ComboBox*> (
        component.findChildWithID ("reference-check"));
    auto* one = dynamic_cast<juce::TextButton*> (
        component.findChildWithID ("reference-blind-1"));
    auto* two = dynamic_cast<juce::TextButton*> (
        component.findChildWithID ("reference-blind-2"));
    auto* answer = dynamic_cast<juce::TextButton*> (
        component.findChildWithID ("reference-blind-answer"));
    auto* reveal = dynamic_cast<juce::TextButton*> (
        component.findChildWithID ("reference-blind-reveal"));
    auto* endBlind = dynamic_cast<juce::TextButton*> (
        component.findChildWithID ("reference-blind-end"));
    KIRIN_REF_REQUIRE (a != nullptr && b != nullptr && b->isEnabled()
                       && startBlind != nullptr && startBlind->isVisible()
                       && one != nullptr && two != nullptr && answer != nullptr
                       && reveal != nullptr
                       && endBlind != nullptr);
    KIRIN_REF_REQUIRE (startBlind->getButtonText() == "BLIND 300%"
                       && startBlind->getY() > b->getBottom());
    KIRIN_REF_REQUIRE (compactCheck != nullptr && compactCheck->isVisible()
                       && compactCandidate != nullptr && compactCandidate->isVisible()
                       && compactCheck->getTitle() == "Check"
                       && compactCandidate->getTitle() == "B Source"
                       && compactCheck->getY() == compactCandidate->getY()
                       && compactCheck->getRight() < compactCandidate->getX()
                       && compactCandidate->getY() > b->getBottom()
                       && compactCandidate->getBottom() < startBlind->getY());
    auto* compactPreset = dynamic_cast<juce::ComboBox*> (component.findChildWithID ("reference-preset"));
    KIRIN_REF_REQUIRE (compactPreset != nullptr && compactPreset->isVisible()
                       && compactPreset->getBottom() <= compactCheck->getY());
    compactPreset->setSelectedId (2, juce::sendNotificationSync);
    KIRIN_REF_REQUIRE (requestedPreset == "preset-b");
    auto* connection = dynamic_cast<juce::Label*> (component.findChildWithID ("reference-connection"));
    KIRIN_REF_REQUIRE (connection != nullptr && connection->getTitle() == "Waiting for Kirin OS");
    compactCheck->setSelectedId (2, juce::sendNotificationSync);
    compactCandidate->setSelectedId (2, juce::sendNotificationSync);
    KIRIN_REF_REQUIRE (requestedCheck == "check-b"
                       && requestedCandidate == "reference-b");
    auto unavailableBlind = readyState();
    unavailableBlind.blindPhase = reference_ui::BlindPhase::unavailable;
    component.setState (unavailableBlind);
    KIRIN_REF_REQUIRE (! startBlind->isVisible() && a->isVisible() && b->isVisible());
    component.setState (readyState());
    a->onClick();
    b->onClick();
    startBlind->onClick();
    KIRIN_REF_REQUIRE (requestedA && requestedB && requestedBlind);

    auto startingState = readyState();
    startingState.blindPhase = reference_ui::BlindPhase::starting;
    startingState.status = "BLIND / WAITING FOR FIRST AUDIBLE BLOCK";
    component.setState (startingState);
    KIRIN_REF_REQUIRE (! a->isVisible() && ! b->isVisible() && ! startBlind->isVisible()
                       && ! one->isVisible() && ! two->isVisible()
                       && ! answer->isVisible() && ! reveal->isVisible()
                       && endBlind->isVisible());

    auto blindState = readyState();
    blindState.blindPhase = reference_ui::BlindPhase::active;
    blindState.activeBlindStimulus = 1;
    blindState.pendingBlindStimulus = 2;
    component.setState (blindState);
    KIRIN_REF_REQUIRE (! a->isVisible() && ! b->isVisible() && ! startBlind->isVisible()
                       && one->isVisible() && two->isVisible() && ! answer->isVisible()
                       && ! reveal->isVisible()
                       && endBlind->isVisible() && one->isEnabled() && ! two->isEnabled());
    blindState.pendingBlindStimulus = 0;
    component.setState (blindState);
    one->onClick();
    two->onClick();
    KIRIN_REF_REQUIRE (! answer->isVisible() && ! reveal->isVisible());
    blindState.activeBlindStimulus = 2;
    blindState.blindStimulusOneHeard = true;
    blindState.blindStimulusTwoHeard = true;
    component.setState (blindState);
    KIRIN_REF_REQUIRE (answer->isVisible() && ! reveal->isVisible());
    answer->onClick();
    KIRIN_REF_REQUIRE (requestedAnswer == 2);
    blindState.answeredBlindStimulus = 2;
    component.setState (blindState);
    KIRIN_REF_REQUIRE (answer->isVisible() && reveal->isVisible());
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

    auto invalidated = blindState;
    invalidated.blindPhase = reference_ui::BlindPhase::invalidated;
    invalidated.blindRequiredAAttenuationDb = 4.2;
    invalidated.status = "BLIND STOPPED / A HELD -4.2 dB / RETURN A EXPLICITLY";
    component.setState (invalidated);
    const auto invalidatedA = render (component);
    invalidated.title = "Must remain concealed after invalidation";
    invalidated.sourceLabel = "WORK VERSION";
    invalidated.adjustedBIntegratedLoudness = 12.0;
    component.setState (invalidated);
    const auto invalidatedB = render (component);
    KIRIN_REF_REQUIRE (! a->isVisible() && ! b->isVisible()
                       && ! one->isVisible() && ! two->isVisible()
                       && endBlind->isVisible()
                       && endBlind->getButtonText().contains ("+4.2 dB")
                       && differentPixels (invalidatedA, invalidatedB) == 0);

    blindState.blindPhase = reference_ui::BlindPhase::revealed;
    blindState.blindReveal = "1 = B  /  2 = A";
    component.setState (blindState);
    const auto revealed = render (component);
    writeImageIfRequested (revealed, "KIRIN_REFERENCE_UI_REVEALED_OUTPUT");
    KIRIN_REF_REQUIRE (! reveal->isVisible()
                       && differentPixels (concealedB, revealed) > 100);

    auto selected = readyState();
    selected.blindLargeScreen = false;
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

    component.setPresentationContext (presentation::forEditor (900, 600));
    component.setSize (888, 470);
    KIRIN_REF_REQUIRE (component.detailedLayout());
    selected.blindLargeScreen = true;
    component.setState (selected);
    auto* preset = dynamic_cast<juce::ComboBox*> (
        component.findChildWithID ("reference-preset"));
    auto* check = dynamic_cast<juce::ComboBox*> (
        component.findChildWithID ("reference-check"));
    auto* candidate = dynamic_cast<juce::ComboBox*> (
        component.findChildWithID ("reference-candidate"));
    auto* cue = dynamic_cast<juce::ComboBox*> (
        component.findChildWithID ("reference-cue"));
    KIRIN_REF_REQUIRE (preset != nullptr && check != nullptr && candidate != nullptr
                       && cue != nullptr && preset->isVisible() && check->isVisible()
                       && candidate->isVisible() && cue->isVisible());
    KIRIN_REF_REQUIRE (candidate->getTooltip().contains ("Replace B")
                       && candidate->getNumItems() == 2);
    preset->setSelectedId (2, juce::sendNotificationSync);
    check->setSelectedId (2, juce::sendNotificationSync);
    candidate->setSelectedId (2, juce::sendNotificationSync);
    cue->setSelectedId (2, juce::sendNotificationSync);
    KIRIN_REF_REQUIRE (requestedPreset == "preset-b" && requestedCheck == "check-b"
                       && requestedCandidate == "reference-b" && requestedCue == "cue-b");
    auto pendingCandidate = selected;
    pendingCandidate.bSelected = false;
    pendingCandidate.auditionBuffered = false;
    pendingCandidate.status = "KIRIN OS PREPARING REFERENCE / A REMAINS LIVE";
    pendingCandidate.candidateId = "reference-b";
    pendingCandidate.candidates[1].label = "Mix v3  /  PREPARE";
    pendingCandidate.candidatePreparationPending = true;
    component.setState (pendingCandidate);
    KIRIN_REF_REQUIRE (candidate->isEnabled() && candidate->getSelectedId() == 2
                       && candidate->getItemText (1).contains ("PREPARE")
                       && ! cue->isEnabled() && ! b->isEnabled());
    writeImageIfRequested (render (component), "KIRIN_REFERENCE_UI_PENDING_OUTPUT");
    reference_ui::Component compactPendingComponent;
    compactPendingComponent.setPresentationContext (presentation::forEditor (300, 200));
    compactPendingComponent.setSize (288, 136);
    compactPendingComponent.setState (pendingCandidate);
    auto* compactPendingSelector = dynamic_cast<juce::ComboBox*> (
        compactPendingComponent.findChildWithID ("reference-candidate"));
    KIRIN_REF_REQUIRE (compactPendingSelector != nullptr
                       && compactPendingSelector->isVisible()
                       && compactPendingSelector->getSelectedId() == 2);
    writeImageIfRequested (render (compactPendingComponent),
                           "KIRIN_REFERENCE_UI_PENDING_COMPACT_OUTPUT");
    auto approval = selected;
    approval.sampleRateApprovalRequired = true;
    approval.actionText = "USE 44.1 TO 48.0 kHz";
    component.setState (approval);
    auto* action = dynamic_cast<juce::TextButton*> (
        component.findChildWithID ("reference-action"));
    KIRIN_REF_REQUIRE (action != nullptr && action->isVisible());
    action->onClick();
    KIRIN_REF_REQUIRE (requestedAction);
    auto visual = selected;
    visual.viewBindings = { "spectrum_low", "loudness", "stereo" };
    visual.liveSpectrumMinimumHz = 20.0f;
    visual.liveSpectrumMaximumHz = 20'000.0f;
    visual.liveSpectrumDbfs.resize (256);
    for (size_t index = 0; index < visual.liveSpectrumDbfs.size(); ++index)
        visual.liveSpectrumDbfs[index] = -54.0f + static_cast<float> (index % 19) * 0.7f;
    component.setState (visual);
    const auto detailed = render (component);
    KIRIN_REF_REQUIRE (detailed.getPixelAt (
        detailed.getWidth() / 4, detailed.getHeight() / 2).getAlpha() != 0);
    writeImageIfRequested (detailed, "KIRIN_REFERENCE_UI_OUTPUT");

    auto abc = visual;
    abc.separateComparisons = true;
    abc.libraryReceived = true;
    abc.osOnline = true;
    abc.versionReady = true;
    abc.checkReady = true;
    abc.versions = { { "version-1", "Mix v3" }, { "version-2", "Mix v2" } };
    abc.versionId = "version-1";
    abc.comparisonSlot = 2;
    abc.audibleComparisonSlot = 2;
    auto* c = dynamic_cast<juce::TextButton*> (component.findChildWithID ("reference-c"));
    auto* version = dynamic_cast<juce::ComboBox*> (component.findChildWithID ("reference-version"));
    KIRIN_REF_REQUIRE (c != nullptr && version != nullptr);
    bool requestedC = false;
    juce::String requestedVersion;
    component.onSelectC = [&] { requestedC = true; };
    component.onSelectVersion = [&] (const auto& id) { requestedVersion = id; };
    for (const auto width : { 300, 375, 450, 600, 900 })
    {
        component.setPresentationContext (presentation::forEditor (width, width * 2 / 3));
        component.setSize (width - 12, width == 900 ? 470 : width * 2 / 3 - 64);
        abc.blindLargeScreen = width == 900;
        component.setState (abc);
        KIRIN_REF_REQUIRE (a->isVisible() && b->isVisible() && c->isVisible()
            && c->getToggleState() && ! b->getToggleState() && ! a->getToggleState());
        KIRIN_REF_REQUIRE (version->isVisible() && check->isVisible() && preset->isVisible()
            && ! version->getBounds().intersects (check->getBounds())
            && component.getLocalBounds().contains (version->getBounds())
            && component.getLocalBounds().contains (check->getBounds()));
        if (width == 300) writeImageIfRequested (render (component), "KIRIN_REFERENCE_UI_ABC_COMPACT_OUTPUT");
        if (width == 900) writeImageIfRequested (render (component), "KIRIN_REFERENCE_UI_ABC_OUTPUT");
    }
    c->onClick();
    version->setSelectedId (2, juce::sendNotificationSync);
    KIRIN_REF_REQUIRE (requestedC && requestedVersion == "version-2" && component.state().checkId == abc.checkId);
    abc.readiness = reference_ui::Readiness::waiting;
    abc.osAccess = os_access::State::connectedUnprepared;
    abc.checkReady = false;
    component.setState (abc);
    KIRIN_REF_REQUIRE (b->isEnabled() && ! c->isEnabled() && a->isEnabled());
    component.setState (visual);

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
        stream.setPosition (0); stream.truncate();
        KIRIN_REF_REQUIRE (juce::PNGImageFormat().writeImageToStream (composite, stream));
    }
}
}
