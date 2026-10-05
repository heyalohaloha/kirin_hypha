#pragma once

#include "../src/HyphaComparisonSurfaceMaterial.h"
#include "../src/HyphaReferenceComponent.h"
#include "../src/HyphaReferenceWindowMaterial.h"
#include "ReferenceGuidanceReview.h"

#include <cstdlib>
#include <iostream>

namespace hypha::tests::reference_state_light
{
inline void expect (bool ok, const juce::String& message)
{
    if (ok) return;
    std::cerr << "Reference state light contract failed: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

inline reference_ui::State fixture (const char* name)
{
    for (const auto& item : reference_review::cases())
        if (juce::String (item.name) == name) return item.state;
    expect (false, juce::String ("missing fixture ") + name);
    return {};
}

inline juce::Image render (juce::Component& component)
{
    juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics g (image);
    g.fillAll (BG);
    component.paintEntireComponent (g, true);
    return image;
}

inline bool sameColour (juce::Colour a, juce::Colour b)
{
    return std::abs (int (a.getRed()) - int (b.getRed())) <= 1
        && std::abs (int (a.getGreen()) - int (b.getGreen())) <= 1
        && std::abs (int (a.getBlue()) - int (b.getBlue())) <= 1;
}

inline void requireQuietEdge (reference_ui::Component& panel, juce::Rectangle<int> body,
                             float alpha, const juce::String& where)
{
    expect (body.getWidth() > 10 && body.getHeight() > 8, where + ": quiet body has room");
    const auto actual = render (panel);
    juce::Image expected (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
    juce::Graphics g (expected);
    g.fillAll (BG);
    const key_light::Scope light (panel);
    comparison_surface::paintQuietBody (g, body.toFloat(), alpha, 4.0f);
    // The left wall lies outside the text and decorative comparison roots. Compare actual
    // pixels, so a separate instrument frame or a state-specific flat fill cannot pass.
    for (int y = body.getY() + body.getHeight() / 3; y < body.getBottom() - body.getHeight() / 3; ++y)
        for (int x = body.getX() + 1; x <= body.getX() + 2; ++x)
            expect (sameColour (actual.getPixelAt (x, y), expected.getPixelAt (x, y)),
                    where + ": anonymous and instructional bodies use the shared quiet material");
}

inline juce::Rectangle<int> blindBody (const reference_ui::Component& panel, bool active)
{
    auto area = panel.panelArea();
    area.removeFromTop (panel.panelHeaderHeight() + panel.panelGap());
    const int line = panel.detailedLayout() ? 24 : 18;
    if (active) area.removeFromBottom (line);
    area.removeFromTop (line + 2);
    return area;
}

inline juce::Rectangle<int> metricsBody (const reference_ui::Component& panel)
{
    auto area = panel.panelArea();
    area.removeFromTop (panel.panelHeaderHeight() + panel.panelGap());
    area.removeFromBottom (panel.detailedLayout() ? 24 : 18);
    return area;
}

inline void requireSharedPlate (juce::Button& button, float inset, juce::Colour accent,
                                float corner, const juce::String& where)
{
    const auto actual = render (button);
    juce::Image expected (juce::Image::ARGB, button.getWidth(), button.getHeight(), true);
    juce::Graphics g (expected);
    g.fillAll (BG);
    const key_light::Scope light (button);
    surface_material::paintControl (g, button.getLocalBounds().toFloat().reduced (inset),
        false, false, button.getToggleState(), accent, corner);
    expect (button.getWidth() >= 20 && button.getHeight() >= 12, where + ": command remains usable");
    for (int x = 6; x < button.getWidth() - 6; x += 3)
        expect (sameColour (actual.getPixelAt (x, 1), expected.getPixelAt (x, 1)),
                where + ": the actual command catches the editor light through the shared plate");
}

inline void writeIfRequested (const reference_ui::State& state, int width, const juce::String& name)
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_REFERENCE_REVIEW_DIR", {});
    if (path.isEmpty()) return;
    const juce::File directory (path);
    expect (directory.createDirectory().wasOk(), "state screenshot directory");
    const auto prefix = i18n::current() == i18n::Language::japanese ? "ja_" : "";
    const auto image = reference_review::render ({ "state", state }, width, width * 2 / 3);
    juce::FileOutputStream stream (directory.getChildFile (
        juce::String (prefix) + "state-" + juce::String (width) + "-" + name + ".png"));
    expect (stream.setPosition (0) && stream.truncate().wasOk()
                && juce::PNGImageFormat().writeImageToStream (image, stream),
            "state screenshot " + name);
}

inline void verifyBlind (reference_ui::Component& panel, int width)
{
    const auto where = "Blind at " + juce::String (width);
    int audioCommands = 0;
    panel.onSelectA = panel.onSelectB = panel.onSelectC = [&] { ++audioCommands; };
    panel.onStartBlind = panel.onRevealBlind = panel.onEndBlind = [&] { ++audioCommands; };
    panel.onSelectBlindStimulus = [&] (int) { ++audioCommands; };
    auto state = fixture ("ready");
    state.comparisonSlot = 1;
    state.title = "Mix v4";
    state.blindLargeScreen = width >= 900;
    state.blindPhase = reference_ui::BlindPhase::available;
    panel.setState (state);
    auto* launch = dynamic_cast<juce::Button*> (panel.findChildWithID ("reference-blind"));
    expect (launch && launch->isVisible(), where + ": named audition exposes its separate trial entry");
    requireSharedPlate (*launch, 1.0f, COL_FLORA_BR, 4.0f, where + " launch");
    writeIfRequested (state, width, "blind-named");
    for (const auto phase : { reference_ui::BlindPhase::starting, reference_ui::BlindPhase::active,
                              reference_ui::BlindPhase::revealed, reference_ui::BlindPhase::invalidated })
    {
        state.blindPhase = phase;
        state.activeBlindStimulus = 2;
        state.blindStimulusOneHeard = state.blindStimulusTwoHeard = phase == reference_ui::BlindPhase::revealed;
        state.blindRequiredAAttenuationDb = phase == reference_ui::BlindPhase::invalidated ? 2.0 : 0.0;
        state.status = phase == reference_ui::BlindPhase::starting ? "PREPARING BLIND"
            : phase == reference_ui::BlindPhase::invalidated ? "BLIND INTERRUPTED / A LEVEL HELD" : "BLIND ACTIVE";
        state.blindReveal = phase == reference_ui::BlindPhase::revealed ? "1 = A / 2 = MIX V4" : "";
        panel.setState (state);
        expect (panel.observationWindowBounds().isEmpty(), where + ": a trial never gains a hero frame");
        for (const auto* id : { "reference-a", "reference-b", "reference-c", "reference-version",
                                "reference-check", "reference-comparison-view", "reference-tonal-view",
                                "capture-a-controls" })
            expect (!panel.findChildWithID (id)->isVisible(), where + ": named sources and measurements stay hidden");
        const bool revealed = phase == reference_ui::BlindPhase::revealed;
        if (revealed)
        {
            auto body = metricsBody (panel);
            const int gap = panel.detailedLayout() ? 6 : 4;
            body = body.removeFromLeft (juce::roundToInt ((body.getWidth() - gap) * 0.5f));
            requireQuietEdge (panel, body, panel.detailedLayout() ? 0.66f : 0.72f, where + " revealed");
        }
        else
            requireQuietEdge (panel, blindBody (panel, phase == reference_ui::BlindPhase::active), 0.72f, where);
        for (const auto* id : { "reference-blind-1", "reference-blind-2", "reference-blind-reveal", "reference-blind-end" })
            if (auto* button = dynamic_cast<juce::Button*> (panel.findChildWithID (id)); button && button->isVisible())
                requireSharedPlate (*button, 1.0f, COL_SPECTRUM_DELTA_BR, 4.0f, where + " " + id);
        if (phase == reference_ui::BlindPhase::active || revealed)
        {
            const auto* one = dynamic_cast<juce::Button*> (panel.findChildWithID ("reference-blind-1"));
            const auto* two = dynamic_cast<juce::Button*> (panel.findChildWithID ("reference-blind-2"));
            expect (one && two && !one->getToggleState() && two->getToggleState(),
                    where + ": lighting never changes the assigned audible stimulus");
        }
        const auto name = phase == reference_ui::BlindPhase::starting ? "blind-starting"
            : phase == reference_ui::BlindPhase::active ? "blind-active"
            : revealed ? "blind-revealed" : "blind-invalid-return";
        writeIfRequested (state, width, name);
    }
    state = fixture ("ready"); state.comparisonSlot = 1; state.blindLargeScreen = width >= 900;
    panel.setState (state);
    expect (!panel.observationWindowBounds().isEmpty()
                && panel.findChildWithID ("reference-comparison-view")->isVisible(),
            where + ": ending the trial restores the normal observation");
    expect (audioCommands == 0, where + ": repaint and restoration issue no audio command");
    writeIfRequested (state, width, "blind-restored");
    panel.onSelectA = panel.onSelectB = panel.onSelectC = {};
    panel.onStartBlind = panel.onRevealBlind = panel.onEndBlind = {};
    panel.onSelectBlindStimulus = {};
}

inline void verifyWorkflowAndApproval (reference_ui::Component& panel, int width)
{
    for (const auto* name : { "stopped", "loading_audio", "approve_b_rate", "queued_c_ceiling" })
    {
        const auto state = fixture (name);
        panel.setState (state);
        if (reference_ui::guide (state).shown)
        {
            auto body = panel.panelArea();
            body.removeFromTop (panel.panelHeaderHeight() + panel.panelGap()
                + (panel.detailedLayout() ? 40 : panel.panelPickerHeight())
                + (!state.presets.empty() || state.libraryReceived
                    ? (panel.detailedLayout() ? 38 : panel.panelPickerHeight()) : 0)
                + panel.panelGap());
            const auto footer = body.removeFromBottom (panel.detailedLayout()
                ? (state.sampleRateApprovalRequired ? 32 : 24) : 18);
            if (state.readiness != reference_ui::Readiness::rejected && state.actionText.isEmpty())
                body = body.getUnion (footer);
            requireQuietEdge (panel, body, 0.72f, name);
        }
        for (const auto* id : { "reference-action", "reference-visual-slot" })
            if (auto* button = dynamic_cast<juce::Button*> (panel.findChildWithID (id)); button && button->isVisible())
                requireSharedPlate (*button, 1.0f, state.sampleRateApprovalRequired && juce::String (id) == "reference-action"
                    ? COL_FLORA_BR : COL_SPECTRUM_DELTA_BR, 4.0f, juce::String (name) + " command");
        writeIfRequested (state, width, name);
    }
    using Workflow = reference_audition::WorkflowView;
    for (const auto status : { Workflow::Status::available, Workflow::Status::preparing, Workflow::Status::ready,
                               Workflow::Status::saving, Workflow::Status::rejected, Workflow::Status::resumeAvailable })
    {
        auto state = fixture ("ready"); state.comparisonSlot = 1;
        state.workflow.mode = status == Workflow::Status::available ? Workflow::Mode::normal : Workflow::Mode::review;
        state.workflow.status = status; state.workflow.reviewAvailable = state.workflow.bookmarkAvailable = true;
        state.workflow.canMoveBack = state.workflow.canAdvance = state.workflow.canEnd = true;
        state.workflow.itemTitle = "Level and balance"; state.workflow.itemCount = 3;
        panel.setState (state);
        int commands = 0;
        for (auto* child : panel.getChildren())
            if (auto* workflow = dynamic_cast<reference_ui::WorkflowControls*> (child); workflow && workflow->isVisible())
                for (auto* command : workflow->getChildren())
                    if (auto* button = dynamic_cast<juce::Button*> (command); button && button->isVisible())
                    {
                        requireSharedPlate (*button, 0.5f, COL_TEXT_SECONDARY, 3.0f, "workflow command");
                        const auto bounds = panel.getLocalArea (button, button->getLocalBounds());
                        expect (panel.getLocalBounds().contains (bounds)
                                    && !panel.observationWindowBounds().intersects (bounds),
                                "workflow commands remain outside the observation at " + juce::String (width));
                        ++commands;
                    }
        expect (commands >= 2, "workflow state retains reachable commands");
        writeIfRequested (state, width, "workflow-" + juce::String (int (status)));
    }
}

inline void verifyCaptureRestoration (reference_ui::Component& panel, int width)
{
    auto access = std::make_shared<reference_audition::ACaptureAccess>();
    auto state = fixture ("ready"); state.comparisonSlot = 1; state.captureAccess = access;
    const auto restore = access->beginRestore ("fixture-only", false);
    expect (restore != 0, "fixture reserves restoration");
    panel.setState (state);
    expect (panel.findChildWithID ("capture-a-controls")->getTitle() == "RESTORING", "restoration stays explicit");
    writeIfRequested (state, width, "capture-restoring");
    expect (access->finishRestore (restore, {}), "fixture finishes failed restoration");
    access->completeRestore (restore);
    reference_audition::ACaptureState failed;
    failed.outcome = { reference_audition::CaptureOutcome::restoreFailed, restore, {} };
    access->publish (failed);
    panel.setState (state);
    expect (panel.findChildWithID ("capture-a-controls")->getDescription().contains ("unavailable"),
            "failed restoration retains its reason");
    writeIfRequested (state, width, "capture-restore-failed");
    auto held = std::make_shared<reference_audition::ACaptureData>();
    held->id = "state-light-fixture"; held->rate = 48000; held->channels = 2; held->frames = 48000;
    held->complete = held->restored = true;
    const auto retry = access->beginRestore ("fixture-retry-only", false);
    expect (access->finishRestore (retry, held), "fixture finishes successful restoration");
    access->completeRestore (retry);
    reference_audition::ACaptureState saved; saved.held = saved.shown = held;
    access->publish (saved);
    panel.setState (state);
    auto* row = panel.findChildWithID ("capture-a-controls");
    expect (row->getTitle() == "CAPTURED", "restoration recovers the held capture without audition");
    for (const auto* id : { "capture-a-action", "capture-a-view" })
    {
        auto* button = dynamic_cast<juce::Button*> (row->findChildWithID (id));
        expect (button && button->isVisible(), "capture state retains its actions");
        requireSharedPlate (*button, 0.5f, COL_MUTED, 3.0f, "restored capture command");
    }
    writeIfRequested (state, width, "capture-restored");
}

inline void verifyTonalCells (juce::Component& root, int width)
{
    reference_ui::TonalView tonal;
    root.addAndMakeVisible (tonal);
    const auto context = presentation::forEditor (width, width * 2 / 3);
    tonal.setBounds (18, 55, width - 36, juce::jmax (80, width * 2 / 3 - 90));
    tonal.update ({}, context, false, {}, {});
    const auto actual = render (tonal);
    juce::Image expected (juce::Image::ARGB, tonal.getWidth(), tonal.getHeight(), true);
    juce::Graphics g (expected); g.fillAll (BG);
    const key_light::Scope light (tonal);
    surface_material::paintPanel (g, tonal.getLocalBounds().toFloat(), 0.72f);
    reference_ui::window_material::paintInterior (g, tonal.getLocalBounds().toFloat(), tonal.getLocalBounds().toFloat());
    for (const auto& cell : tonal.visualLayout().cards)
    {
        surface_material::paintPanel (g, cell, 0.72f, 3.0f);
        const int x = int (std::ceil (cell.getX())) + 1;
        for (int y = int (cell.getCentreY()) - 2; y <= int (cell.getCentreY()) + 2; ++y)
            expect (sameColour (actual.getPixelAt (x, y), expected.getPixelAt (x, y)),
                    "each Balance group uses quiet shared material at " + juce::String (width));
    }
}

inline void verifyDisabledControls (reference_ui::Component& panel)
{
    auto state = fixture ("ready"); state.blindPhase = reference_ui::BlindPhase::active;
    panel.setState (state);
    auto* reveal = dynamic_cast<juce::Button*> (panel.findChildWithID ("reference-blind-reveal"));
    expect (reveal && !reveal->isEnabled(), "unheard sources keep Reveal disabled");
    const auto disabled = render (*reveal);
    for (const auto next : { juce::Button::buttonOver, juce::Button::buttonDown })
    {
        reveal->setState (next);
        const auto changed = render (*reveal);
        for (int x = 6; x < reveal->getWidth() - 6; ++x)
            expect (disabled.getPixelAt (x, 1) == changed.getPixelAt (x, 1),
                    "disabled Reveal cannot acquire a hover or pressed highlight");
    }
    reference_ui::ReferenceSelectorLookAndFeel look;
    juce::ComboBox selector; panel.addAndMakeVisible (selector); selector.setBounds (20, 20, 120, 24);
    selector.setEnabled (false);
    const auto draw = [&] (bool down) {
        juce::Image image (juce::Image::ARGB, 120, 24, true); juce::Graphics g (image); g.fillAll (BG);
        look.drawComboBox (g, 120, 24, down, 0, 0, 0, 0, selector); return image;
    };
    const auto idle = draw (false), pressed = draw (true);
    for (int y = 0; y < 24; ++y) for (int x = 0; x < 120; ++x)
        expect (idle.getPixelAt (x, y) == pressed.getPixelAt (x, y),
                "disabled selectors keep both their bevel and arrow at rest");
}

inline void verifyAccess (juce::Component& root, observatory::View& shell, int width)
{
    reference_ui::AccessPanel panel;
    root.addAndMakeVisible (panel);
    panel.setPresentationContext (presentation::forEditor (width, width * 2 / 3));
    panel.setBounds (shell.analysisBodyBounds());
    int aboutCommands = 0, recheckCommands = 0;
    panel.onAbout = [&] { ++aboutCommands; };
    panel.onRecheck = [&] { ++recheckCommands; };
    const auto action = [&] (const char* id) {
        auto* button = dynamic_cast<juce::Button*> (panel.findChildWithID (id));
        expect (button != nullptr, "access action exists"); return button;
    };
    auto* about = action ("reference-access-about");
    auto* owner = action ("reference-access-owner");
    auto* recheck = action ("reference-access-recheck");
    const auto heading = panel.findChildWithID ("reference-access-heading")->getBounds();
    const auto aboutBounds = about->getBounds(), ownerBounds = owner->getBounds();
    const auto verifyState = [&] (const char* name, bool help, bool owned) {
        const auto actual = render (panel);
        juce::Image expected (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
        juce::Graphics g (expected); g.fillAll (BG);
        const key_light::Scope light (panel);
        comparison_surface::paintQuietBody (g, panel.getLocalBounds().toFloat(), 0.72f, 4.0f);
        int materialPixels = 0;
        // The full quiet wall stays outside the existing 4 px content inset in every state.
        for (int y = panel.getHeight() / 4; y < panel.getHeight() * 3 / 4; ++y)
            for (int x = 1; x <= 2; ++x)
            {
                expect (sameColour (actual.getPixelAt (x, y), expected.getPixelAt (x, y)),
                        juce::String (name) + ": access uses the common quiet body");
                materialPixels += actual.getPixelAt (x, y) != BG;
            }
        expect (materialPixels > 10, "access guidance has an actual recessed wall");
        expect (panel.findChildWithID ("reference-access-heading")->getBounds() == heading
                    && owner->getBounds() == ownerBounds && about->getBounds() == aboutBounds,
                "access lighting preserves existing heading and action geometry");
        expect (about->isVisible() == (!help && !owned) && owner->isVisible() == !owned
                    && recheck->isVisible() == (help && !owned), "access state retains its exact actions");
        for (auto* child : panel.getChildren())
            if (child->isVisible())
            {
                expect (panel.getLocalBounds().reduced (4).contains (child->getBounds()),
                        "access content keeps its 4 px inset at " + juce::String (width));
                if (auto* button = dynamic_cast<juce::Button*> (child))
                {
                    expect (button->isEnabled() && button->getWantsKeyboardFocus()
                                && button->getHeight() >= 28
                                && button->findColour (juce::TextButton::textColourOffId) == COL_NORMAL,
                            "access actions retain their enabled, keyboard and colour semantics");
                    requireSharedPlate (*button, 1.0f, COL_SPECTRUM_DELTA_BR, 3.0f, name);
                }
            }
        const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_REFERENCE_REVIEW_DIR", {});
        if (path.isEmpty()) return;
        const juce::File directory (path);
        expect (directory.createDirectory().wasOk(), "access screenshot directory");
        auto image = compact_review::renderShell (shell);
        juce::Graphics drawing (image);
        drawing.addTransform (juce::AffineTransform::translation (float (panel.getX()), float (panel.getY()))
                                  .scaled (compact_review::dpi));
        panel.paintEntireComponent (drawing, true);
        const auto prefix = i18n::current() == i18n::Language::japanese ? "ja_" : "";
        juce::FileOutputStream stream (directory.getChildFile (
            juce::String (prefix) + "state-" + juce::String (width) + "-access-" + name + ".png"));
        expect (stream.setPosition (0) && stream.truncate().wasOk()
                    && juce::PNGImageFormat().writeImageToStream (image, stream), "access screenshot");
    };
    verifyState ("unowned", false, false);
    expect (aboutCommands == 0 && recheckCommands == 0, "access opening and paint issue no commands");
    const auto discovery = panel.getDescription();
    about->onClick();
    verifyState ("about", false, false);
    expect (aboutCommands == 1 && recheckCommands == 0 && panel.getDescription() == discovery,
            "About remains an explicit request and changes no entitlement");
    owner->onClick();
    verifyState ("owner-help", true, false);
    expect (owner->getButtonText() == "BACK" && aboutCommands == 1 && recheckCommands == 0,
            "owner help only changes guidance");
    panel.setRecheckUnconfirmed (true);
    verifyState ("recheck-unconfirmed", true, false);
    expect (panel.getDescription().contains ("License not confirmed"), "failed recheck keeps its reason");
    recheck->onClick();
    expect (aboutCommands == 1 && recheckCommands == 1, "local recheck stays explicit and fixture-only");
    panel.setOwned (true);
    verifyState ("owned-waiting", true, true);
    expect (!panel.getDescription().contains ("License not confirmed")
                && panel.getDescription().contains ("Saved Reference presets"), "recognized ownership restores delivery guidance");
    panel.setOwned (false);
    verifyState ("unowned-restored", false, false);
    expect (panel.getDescription() == discovery && aboutCommands == 1 && recheckCommands == 1,
            "restoring discovery changes no license, OS or audition state");
}

inline void verify()
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        for (const auto& preset : observatory::sizePresets)
        {
            observatory::View shell (observatory::Role::post); shell.setSize (preset.width, preset.height);
            shell.setDomain (observatory::Domain::reference); shell.setExternalAnalysisBodyActive (true);
            juce::Component root; root.getProperties().set (key_light::rootProperty, true);
            root.setSize (preset.width, preset.height);
            reference_ui::Component panel; root.addAndMakeVisible (panel);
            panel.setPresentationContext (presentation::forEditor (preset.width, preset.height));
            panel.setBounds (shell.analysisBodyBounds());
            verifyBlind (panel, preset.width);
            verifyWorkflowAndApproval (panel, preset.width);
            verifyCaptureRestoration (panel, preset.width);
            verifyTonalCells (root, preset.width);
            verifyDisabledControls (panel);
            verifyAccess (root, shell, preset.width);
        }
    }
}
}
