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

// The control with this ID anywhere under root, and whether it and its parents up to root are visible.
inline const juce::Component* findById (const juce::Component& root, const juce::String& id)
{
    for (auto* child : root.getChildren())
    {
        if (child->getComponentID() == id) return child;
        if (const auto* found = findById (*child, id)) return found;
    }
    return nullptr;
}

inline bool visibleIn (const juce::Component& root, const juce::Component* control)
{
    for (auto* item = control; item != nullptr && item != &root; item = item->getParentComponent())
        if (! item->isVisible()) return false;
    return control != nullptr;
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

// VERSION BLIND の試行のあいだ、REF は主役の窓も、比べる音源の名前と値も出さない（窓全体はエディターの Blind の
// 画面が持つ。その素材は BlindLightContract.h）。終われば元の観測に戻り、描き直しは音の命令を出さない。
inline void verifyBlind (reference_ui::Component& panel, int width)
{
    const auto where = "Blind at " + juce::String (width);
    int audioCommands = 0;
    panel.onSelectA = panel.onSelectB = panel.onSelectC = panel.onSelectRef = [&] { ++audioCommands; };
    panel.onStartBlind = [&] { ++audioCommands; };
    auto state = fixture ("ready");
    state.comparisonSlot = 1;
    state.title = "Mix v4";
    state.blindLargeScreen = width >= 900;
    state.blindPhase = reference_ui::BlindPhase::available;
    panel.setState (state);
    if (auto* launch = dynamic_cast<juce::Button*> (const_cast<juce::Component*> (findById (panel, "reference-blind")));
        visibleIn (panel, launch))
        requireSharedPlate (*launch, 1.0f, COL_FLORA_BR, 4.0f, where + " launch");
    writeIfRequested (state, width, "blind-named");
    for (const auto phase : { reference_ui::BlindPhase::starting, reference_ui::BlindPhase::active,
                              reference_ui::BlindPhase::revealed, reference_ui::BlindPhase::invalidated })
    {
        state.blindPhase = phase;
        state.activeBlindStimulus = 2;
        state.blindStimulusOneHeard = state.blindStimulusTwoHeard = phase == reference_ui::BlindPhase::revealed;
        state.blindRequiredAAttenuationDb = phase == reference_ui::BlindPhase::invalidated ? 2.0 : 0.0;
        panel.setState (state);
        expect (panel.observationWindowBounds().isEmpty(), where + ": a trial never gains a hero frame");
        for (const auto* id : { "reference-comparison-view", "reference-song-list" })
            expect (! visibleIn (panel, findById (panel, id)), where + ": named sources and measurements stay hidden");
        const auto name = phase == reference_ui::BlindPhase::starting ? "blind-starting"
            : phase == reference_ui::BlindPhase::active ? "blind-active"
            : phase == reference_ui::BlindPhase::revealed ? "blind-revealed" : "blind-invalid-return";
        writeIfRequested (state, width, name);
    }
    state = fixture ("ready"); state.comparisonSlot = 1; state.blindLargeScreen = width >= 900;
    panel.setState (state);
    expect (!panel.observationWindowBounds().isEmpty() && visibleIn (panel, findById (panel, "reference-comparison-view")),
            where + ": ending the trial restores the normal observation");
    expect (audioCommands == 0, where + ": repaint and restoration issue no audio command");
    writeIfRequested (state, width, "blind-restored");
    panel.onSelectA = panel.onSelectB = panel.onSelectC = panel.onSelectRef = {};
    panel.onStartBlind = {};
}

inline void verifyDisabledSelectors (reference_ui::Component& panel)
{
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
    panel.removeChildComponent (&selector);
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
            verifyDisabledSelectors (panel);
            verifyAccess (root, shell, preset.width);
        }
    }
}
}
