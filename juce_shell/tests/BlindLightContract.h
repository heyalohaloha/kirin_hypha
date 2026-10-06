#pragma once

#include "../src/HyphaComparisonSurfaceMaterial.h"
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLocalBlindComponent.h"
#include "../src/HyphaObservatoryView.h"
#include "CompactReviewShowcase.h"
#include "SelectMenuContract.h"

#include <cstdlib>
#include <functional>
#include <iostream>

namespace hypha::tests::blind_light_contract
{
inline void require (bool condition, const juce::String& message)
{
    if (condition) return;
    std::cerr << "Blind light contract failed: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

inline juce::Image render (juce::Component& component, bool children)
{
    juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics g (image);
    g.fillAll (BG);
    if (children) component.paintEntireComponent (g, true);
    else component.paint (g);
    return image;
}

inline void requireQuietForm (juce::Component& component)
{
    const auto previous = key_light::current();
    const auto active = key_light::active();
    const auto actual = render (component, false);
    require (key_light::current() == previous && key_light::active() == active,
             "a comparison form restores the light of the painter that called it");
    juce::Image expected (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics g (expected);
    g.fillAll (BG);
    surface_material::paintPanel (g, component.getLocalBounds().toFloat().reduced (10), 0.94f, 7.0f);
    // Only body pixels: the Local screen keeps its existing LED, steps and purpose text. Neither
    // preparation nor a result can add a bronze hero ring or the old independent instrument rim.
    for (int y = 40; y < component.getHeight() - 40; ++y)
        for (const int x : { 2, 5, 12, 13, component.getWidth() - 14, component.getWidth() - 13,
                             component.getWidth() - 6, component.getWidth() - 3 })
            require (actual.getPixelAt (x, y) == expected.getPixelAt (x, y),
                     "all comparison states use the same quiet recessed body");
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

inline void requirePlateLight (juce::Component& screen, juce::Component& control)
{
    const auto screenBounds = screen.getBounds();
    const auto bounds = control.getBounds();
    // Place this actual control on either side of the editor light, regardless of which slot its
    // current state uses. Only the parent position changes; state, selection and text stay fixed.
    screen.setTopLeftPosition (20 - bounds.getX(), screenBounds.getY());
    const auto left = render (control, true);
    screen.setTopLeftPosition (650 - bounds.getX(), screenBounds.getY());
    const auto right = render (control, true);
    screen.setBounds (screenBounds);
    require (bevelBrightness (left, true) > bevelBrightness (right, true)
                 && bevelBrightness (right, false) > bevelBrightness (left, false),
             control.getComponentID() + ": its upper bevel follows the editor's one light");
    require (control.getBounds() == bounds, "lighting preserves a comparison control's layout");
}

inline int differentPixels (const juce::Image&, const juce::Image&);

inline void requireControlLight (juce::Component& screen, juce::Button& button)
{
    const auto text = button.getButtonText();
    const auto selected = button.getToggleState(), enabled = button.isEnabled();
    const auto state = button.getState();
    requirePlateLight (screen, button);
    button.setEnabled (false);
    button.setState (juce::Button::buttonNormal);
    const auto normal = render (button, true);
    button.setState (juce::Button::buttonOver);
    const auto over = render (button, true);
    button.setState (juce::Button::buttonDown);
    const auto down = render (button, true);
    button.setEnabled (enabled);
    button.setState (state);
    require (differentPixels (normal, over) == 0 && differentPixels (normal, down) == 0,
             button.getComponentID() + ": disabled hover and press cannot brighten the actual command");
    require (button.getButtonText() == text && button.getToggleState() == selected
                 && button.isEnabled() == enabled && button.getState() == state,
             "lighting preserves a comparison command's state and layout");
}

inline void requireControls (juce::Component& screen)
{
    int commands = 0;
    for (int index = 0; index < screen.getNumChildComponents(); ++index)
        if (auto* button = dynamic_cast<juce::Button*> (screen.getChildComponent (index));
            button && button->isVisible() && button->getWidth() >= 24 && button->getHeight() >= 8)
        {
            requireControlLight (screen, *button);
            ++commands;
        }
    require (commands > 0, "each comparison state has a real command to verify");
    if (auto* choice = dynamic_cast<juce::ComboBox*> (screen.findChildWithID ("local-blind-context"));
        choice && choice->isVisible())
        requirePlateLight (screen, *choice);
}

inline int differentPixels (const juce::Image& first, const juce::Image& second)
{
    int count = 0;
    for (int y = 0; y < first.getHeight(); ++y)
        for (int x = 0; x < first.getWidth(); ++x)
            count += first.getPixelAt (x, y) != second.getPixelAt (x, y);
    return count;
}

inline void verifyLive (juce::Component& root, const observatory::SizePreset& preset)
{
    using Stage = live_compare::BlindStage;
    live_blind_ui::Component screen;
    root.addAndMakeVisible (screen);
    screen.setBounds (20, 0, preset.width, preset.height);
    for (const auto stage : { Stage::idle, Stage::preparing, Stage::approval, Stage::settling,
                              Stage::active, Stage::invalidated, Stage::finishing, Stage::failed })
    {
        live_compare::LiveBlindStatus state;
        state.stage = stage;
        state.lowerPostDb = -6;
        state.trial.active = stage == Stage::active;
        state.trial.audible = 1;
        state.trial.played = 3;
        state.reason = stage == Stage::invalidated ? live_compare::RecoveryReason::contentChanged
                                                  : live_compare::RecoveryReason::none;
        screen.setState (state, true, 0.5f);
        requireQuietForm (screen);
        requireControls (screen);
    }
    live_compare::LiveBlindStatus hidden;
    hidden.stage = Stage::active;
    hidden.trial.active = true;
    hidden.trial.audible = 1;
    hidden.trial.played = 3;
    screen.setState (hidden, true, 1.0f);
    const auto first = render (screen, true);
    hidden.trial.firstPre = true;
    screen.setState (hidden, true, 1.0f);
    require (differentPixels (first, render (screen, true)) == 0,
             "Live Blind light and pixels never disclose the hidden PRE/POST assignment");
    hidden.trial.revealed = true;
    screen.setState (hidden, true, 1.0f);
    requireQuietForm (screen);
    requireControls (screen);
    require (differentPixels (first, render (screen, true)) > 20,
             "Live Blind reveal still names the sources in the actual result screen");
}

inline void verifyLocal (juce::Component& root, const observatory::SizePreset& preset)
{
    using Phase = local_blind::ProductSessionPhase;
    local_blind_ui::Component screen;
    root.addAndMakeVisible (screen);
    screen.setPresentationContext (presentation::forEditor (preset.width, preset.height));
    screen.setBounds (20, 0, preset.width, preset.height);
    local_blind::ProductSessionView state;
    state.sampleRate = 48'000;
    state.channels = 2;
    state.frames = 192'000;
    state.canRecapture = true;
    state.lowerPostGainDb = -6;
    for (const auto phase : { Phase::idle, Phase::capturing, Phase::preparing, Phase::ready,
                              Phase::armed, Phase::listening, Phase::revealed, Phase::returnPending,
                              Phase::returned, Phase::failed })
    {
        state.phase = phase;
        state.trial.activeStimulus = 1;
        state.trial.lowerPostApprovalRequired = phase == Phase::ready;
        state.trial.canAnswer = phase == Phase::listening;
        screen.setState (state);
        requireQuietForm (screen);
        requireControls (screen);
    }
    for (const auto phase : { Phase::armed, Phase::listening })
    {
        state.phase = phase;
        state.trial.named = true;
        state.trial.canAnswer = false;
        screen.setState (state);
        requireQuietForm (screen);
        requireControls (screen);
        state.trial.named = false;
        state.trial.revealedOneSide = 0;
        screen.setState (state);
        const auto anonymous = render (screen, true);
        state.trial.revealedOneSide = 1;
        screen.setState (state);
        require (differentPixels (anonymous, render (screen, true)) == 0,
                 "Exact4s Blind light and pixels never disclose a mapping before reveal");
    }
    state.phase = Phase::returnPending;
    state.trial.failure = local_blind::TrialFailure::discontinuity;
    state.returnFacts.attenuationApplied = true;
    screen.setState (state);
    requireQuietForm (screen);
    requireControls (screen);
    state.returnFacts = { 1, 1, 1, false, true };
    screen.setState (state);
    requireQuietForm (screen);
    requireControls (screen);
}

inline void verifyNamedFooter (juce::Component& root, const observatory::SizePreset& preset)
{
    observatory::View screen (observatory::Role::post);
    root.addAndMakeVisible (screen);
    screen.setBounds (20, 0, preset.width, preset.height);
    for (int phase = 0; phase < 8; ++phase)
    {
        observatory::LiveCompareFooter state;
        state.entryEnabled = true;
        state.active = phase >= 1 && phase <= 6;
        state.preSelected = phase >= 2;
        state.preWaiting = phase == 2;
        state.matched = phase >= 3;
        state.following = phase == 4;
        state.matchLimited = phase == 5;
        state.finishing = phase == 6;
        state.postHeldTenthsDb = phase >= 5 ? -60 : 0;
        screen.setLiveCompareFooter (state);
        for (const auto* id : { "observatory-live-compare", "observatory-live-pre",
                                "observatory-live-post", "observatory-live-match",
                                "observatory-live-end", "observatory-live-return" })
            if (auto* button = dynamic_cast<observatory::Button*> (screen.findChildWithID (id));
                button && button->isVisible() && ! button->isStatusOnly())
                requireControlLight (screen, *button);
    }
}

inline void writeReview()
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_BLIND_LIGHT_REVIEW_DIR", {});
    if (path.isEmpty()) return;
    const juce::File directory (path);
    require (directory.createDirectory().wasOk(), "comparison light review directory");
    const i18n::ScopedLanguage language (i18n::Language::english);
    material_cache::Lifetime material;
    for (const auto& preset : observatory::sizePresets)
    {
        juce::Component root;
        root.getProperties().set (key_light::rootProperty, true);
        root.setSize (preset.width, preset.height);
        const auto write = [&] (juce::Component& screen, const char* state) {
            juce::Image image (juce::Image::ARGB, preset.width * 2, preset.height * 2, true);
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (2.0f));
            screen.paintEntireComponent (g, true);
            juce::FileOutputStream stream (directory.getChildFile (
                juce::String (preset.width) + "_" + state + ".png"));
            require (stream.setPosition (0) && stream.truncate().wasOk()
                         && juce::PNGImageFormat().writeImageToStream (image, stream),
                     "comparison state review written");
        };
        observatory::View named (observatory::Role::post);
        root.addAndMakeVisible (named);
        named.setBounds (root.getLocalBounds());
        named.setObservatoryFrame (compact_review::frame(), true);
        named.setWatchDisplay (compact_review::watch(), true);
        named.setHistory (compact_review::history());
        named.setConnection ("PAIR REVIEW", COL_LED_BLUE, observatory::ConnectionState::paired);
        const char* footerNames[] { "listen_post", "listen_pre", "listen_wait", "listen_held",
                                    "listen_auto", "listen_lowered", "listen_return", "listen_restoring" };
        for (int phase = 0; phase < 8; ++phase)
        {
            observatory::LiveCompareFooter state;
            state.entryEnabled = true;
            state.active = phase < 6;
            state.preSelected = phase >= 1 && phase <= 4;
            state.preWaiting = phase == 2;
            state.matched = phase != 2;
            state.matchHeld = phase == 3;
            state.following = phase == 4;
            state.preGainTenthsDb = 18;
            state.postHeldTenthsDb = phase >= 5 ? -60 : 0;
            state.finishing = phase == 7;
            named.setLiveCompareFooter (state);
            write (named, footerNames[phase]);
        }
        local_blind_ui::Component exact;
        root.addAndMakeVisible (exact);
        exact.setPresentationContext (presentation::forEditor (preset.width, preset.height));
        exact.setBounds (root.getLocalBounds());
        const char* exactNames[] { "exact_named_pre", "exact_named_post", "exact_named_lowered",
                                   "exact_named_wait", "exact_approval", "exact_blind", "exact_result" };
        for (int phase = 0; phase < 7; ++phase)
        {
            local_blind::ProductSessionView state;
            state.sampleRate = 48'000;
            state.channels = 2;
            state.start = 48'000;
            state.frames = 192'000;
            state.phase = phase == 3 ? local_blind::ProductSessionPhase::armed
                : phase == 4 ? local_blind::ProductSessionPhase::ready
                : phase == 6 ? local_blind::ProductSessionPhase::revealed
                             : local_blind::ProductSessionPhase::listening;
            state.fixedPreGainDb = phase == 2 ? 0 : 3.4;
            state.lowerPostGainDb = phase == 2 || phase == 4 ? -6 : 0;
            state.trial.named = phase < 4;
            state.trial.activeStimulus = phase == 1 || phase == 2 ? 2 : 1;
            state.trial.lowerPostApprovalRequired = phase == 4;
            state.trial.revealedOneSide = phase == 6 ? 1 : -1;
            state.trial.answer = phase == 6 ? local_blind::TrialAnswer::noPreference
                                          : local_blind::TrialAnswer::none;
            exact.setState (state);
            write (exact, exactNames[phase]);
        }
        live_blind_ui::Component live;
        root.addAndMakeVisible (live);
        live.setBounds (root.getLocalBounds());
        const char* liveNames[] { "live_blind", "live_approval", "live_result" };
        for (int phase = 0; phase < 3; ++phase)
        {
            live_compare::LiveBlindStatus state;
            state.stage = phase == 1 ? live_compare::BlindStage::approval : live_compare::BlindStage::active;
            state.lowerPostDb = -6;
            state.trial.active = phase != 1;
            state.trial.audible = 1;
            state.trial.played = 3;
            state.trial.firstPre = true;
            state.trial.revealed = phase == 2;
            live.setState (state, true, phase == 1 ? 1.0f : 0.5f);
            write (live, liveNames[phase]);
        }
    }
}

inline void verify()
{
    juce::Component root;
    root.getProperties().set (key_light::rootProperty, true);
    for (const auto& preset : observatory::sizePresets)
    {
        root.setSize (preset.width, preset.height);
        verifyLive (root, preset);
        verifyLocal (root, preset);
        verifyNamedFooter (root, preset);
    }
    select_menu_contract::verify();
    writeReview();
}
}
