#pragma once
#include "ReferenceSelectionReopenTest.h"
#include "../src/PluginEditor.h"
#include <functional>
#include "../src/HyphaPairLabels.h"
#include <iostream>
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaObservatoryView.h"

namespace hypha::tests
{
inline juce::String candidateMenuExpectedText; // one extra line the next real PRE2 menu must show
// Only schedule the actual editor timer deterministically. Choices still enter through buttons
// and their native menus; no alternate product start or output implementation is installed.
struct CandidateEditorTick
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type candidateEditorTick (CandidateEditorTick);
};
template<class Tag, typename Tag::Type Member> struct CandidateEditorAccess
{ friend typename Tag::Type candidateEditorTick (Tag) { return Member; } };
template struct CandidateEditorAccess<CandidateEditorTick, &KirinHyphaEditor::timerCallback>;
struct CandidateEditorDeadline
{
    using Type = double KirinHyphaEditor::*;
    friend Type candidateEditorTick (CandidateEditorDeadline);
};
template struct CandidateEditorAccess<CandidateEditorDeadline, &KirinHyphaEditor::liveCompareStartAfterReturnUntil>;
inline void expireCandidateEntryDeadline (juce::AudioProcessorEditor& editor)
{ dynamic_cast<KirinHyphaEditor&> (editor).*candidateEditorTick (CandidateEditorDeadline {}) = 1.0; }
inline bool candidateEntryPending (juce::AudioProcessorEditor& editor)
{ return dynamic_cast<KirinHyphaEditor&> (editor).*candidateEditorTick (CandidateEditorDeadline {}) > 0.0; }
struct CandidateEditorWarning
{
    using Type = juce::String KirinHyphaEditor::*;
    friend Type candidateEditorTick (CandidateEditorWarning);
};
template struct CandidateEditorAccess<CandidateEditorWarning, &KirinHyphaEditor::liveCompareWarning>;
inline void candidateTick (juce::AudioProcessorEditor& editor)
{ (dynamic_cast<KirinHyphaEditor&> (editor).*candidateEditorTick (CandidateEditorTick {}))(); }

inline juce::Component* candidateControl (juce::Component& component, const juce::String& id)
{
    if (component.getComponentID() == id) return &component;
    for (auto* child : component.getChildren())
        if (auto* found = candidateControl (*child, id)) return found;
    return nullptr;
}

inline const observatory::View* candidateView (const juce::Component& component)
{
    if (const auto* view = dynamic_cast<const observatory::View*> (&component)) return view;
    for (const auto* child : component.getChildren())
        if (const auto* found = candidateView (*child)) return found;
    return nullptr;
}

inline juce::String candidateMenuText (juce::Component& component)
{
    auto text = component.getAccessibilityHandler() ? component.getAccessibilityHandler()->getTitle() : component.getTitle();
    for (auto* child : component.getChildren()) text += "\n" + candidateMenuText (*child);
    return text;
}

inline bool candidatePressMenuItem (juce::Component& component, const juce::String& label)
{
    if (auto* handler = component.getAccessibilityHandler(); handler && handler->getTitle() == label
        && handler->getActions().invoke (juce::AccessibilityActionType::press)) return true;
    for (auto* child : component.getChildren()) if (candidatePressMenuItem (*child, label)) return true;
    return false;
}

template<class Require> void pressComparisonMenuEntry (juce::AudioProcessorEditor& editor,
                                                      const juce::String& label, Require require)
{
    auto* menu = dynamic_cast<juce::Button*> (candidateControl (editor, "observatory-menu"));
    require (menu && menu->isVisible() && menu->onClick, "the actual MENU remains available during return");
    require (juce::MessageManager::callAsync ([menu] { menu->onClick(); }), "open the product MENU");
    require (afterQueuedMessages ([&]
    {
        auto& modal = *juce::ModalComponentManager::getInstance();
        require (modal.getNumModalComponents() == 1 && candidatePressMenuItem (*modal.getModalComponent (0), label),
                 "activate the requested comparison through the actual MENU item");
    }), "deliver the MENU selection");
}

template<class Require> void verifyCandidateRecovery (KirinHyphaProcessorBase& post, Require require, bool bypassed)
{
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
    require (editor != nullptr, "open the actual recovery surface");
    editor->setVisible (true); candidateTick (*editor);
    const auto warning = dynamic_cast<KirinHyphaEditor&> (*editor).*candidateEditorTick (CandidateEditorWarning {});
    require (warning == (bypassed ? "A/B: enable PRE2; bypass is on"
                                  : "A/B: timing unavailable; check PRE1 and PRE2"),
             "actual recovery distinguishes known bypass from an unverified placement without ending A/B");
    require (post.candidateCompareActive(), "recovery guidance retains the explicit comparison");
    post.editorBeingDeleted (editor.get());
}

template<class Require> void enterCandidateThroughEditor (KirinHyphaProcessorBase& post, Require require,
    std::function<void(juce::AudioProcessorEditor&)> completeReturn = {}, juce::MemoryBlock* beforeStart = nullptr,
    bool throughKey = false)
{
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
    require (editor != nullptr, "A/B opens the real product editor");
    editor->setSize (900, 600); editor->setVisible (true);
    // A key reaches only a showing window, as a host gives it.
    juce::DocumentWindow keysWindow ("Keys", juce::Colours::black, juce::DocumentWindow::minimiseButton);
    if (throughKey)
    {
        keysWindow.setContentNonOwned (editor.get(), true);
        keysWindow.setVisible (true);
        editor->grabKeyboardFocus();
        require (editor->getPeer() != nullptr && editor->isShowing(), "A/B's key needs its native window");
    }
    candidateTick (*editor); // publish the processor's current state before inspecting its controls
    if (beforeStart) post.getStateInformation (*beforeStart);
    const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_CANDIDATE_PREVIEW", {});
    if (preview.isNotEmpty())
    {
        const auto file = juce::File (preview).getChildFile ("product-entry-" + juce::String (int (i18n::current())) + ".png");
        auto stream = file.createOutputStream();
        require (stream && stream->setPosition (0) && stream->truncate().wasOk()
            && juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds()), *stream),
            "actual A/B entry preview is rendered");
    }
    auto* button = dynamic_cast<juce::Button*> (candidateControl (*editor, "observatory-candidate-compare"));
    require (button != nullptr && button->onClick, "A/B is an actual product button");
    require (button->isEnabled(), "A/B entry stays usable while an approved level is held");
    auto& modal = *juce::ModalComponentManager::getInstance();
    int choiceIndex = 0;
    const auto choose = [&]
    {
        if (modal.getNumModalComponents() != 1)
        {
            std::cerr << "candidate menu " << choiceIndex << ": modal=" << modal.getNumModalComponents()
                << ", candidates=" << post.enumeratePreCandidates().size() << ", record=" << post.isRecording()
                << ", keep=" << int (post.keepPhase()) << ", isolated=" << post.candidateMeasurementIsolated() << '\n';
            if (const auto* view = candidateView (*editor))
                std::cerr << "candidate entry feedback: " << view->feedback() << '\n';
        }
        require (modal.getNumModalComponents() == 1, "the requested choice opens once");
        auto* menu = modal.getModalComponent (0);
        require (menu != nullptr, "the actual menu is present");
        const auto root = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_CANDIDATE_PREVIEW", {});
        if (root.isNotEmpty())
        {
            const auto file = juce::File (root).getChildFile ("entry-" + juce::String (int (i18n::current()))
                + "-" + juce::String (choiceIndex) + ".png");
            auto stream = file.createOutputStream();
            require (stream && stream->setPosition (0) && stream->truncate().wasOk()
                && juce::PNGImageFormat().writeImageToStream (menu->createComponentSnapshot (menu->getLocalBounds()), *stream),
                "actual candidate menu preview is rendered");
        }
        const auto text = candidateMenuText (*menu);
        if (choiceIndex == 1)
        {
            require (text.contains (juce::String::fromUTF8 (u8"PRE1 → A → PRE2 → B → POST")),
                     "the real placement menu preserves every UTF-8 arrow");
            const auto hardware = text_style::shownText ("One track. Hardware in A or B.");
            require (text.contains (hardware) || text.contains ("One track. Hardware in A or B."),
                     "the actual placement menu permits hardware on either side");
            require (hardware == (i18n::current() == i18n::Language::english ? "One track. Hardware in A or B."
                : juce::String::fromUTF8 (u8"同じトラック。実機はA・Bどちらにも。")),
                "the hardware placement line renders the complete meaning in either language");
        }
        else
        {
            for (const auto& candidate : post.enumeratePreCandidatesForMenu())
                if (candidate.instanceId != post.pairedPreInstanceId())
                    require (text.contains (hypha::pair_labels::menuLabel (post.enumeratePreCandidatesForMenu(), candidate)),
                             "the real PRE2 menu uses the normal pair label");
            require (text.contains (candidateMenuExpectedText), "the real PRE2 menu shows the expected mark");
        }
        require (std::abs (menu->getScreenBounds().getCentreX() - button->getScreenBounds().getCentreX()) < 300,
                 "both native menus are anchored to the A/B button");
        ++choiceIndex;
        menu->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        menu->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    };
    // This background native host deliberately never takes OS focus. Keep each real menu's
    // opening and keyboard choice in one GUI batch, before its focus-loss timer dismisses it.
    if (throughKey)
        require (juce::MessageManager::callAsync ([peer = editor->getPeer()]
        {
            peer->handleKeyPress (juce::KeyPress ('W'));
            peer->handleKeyUpOrDown (false);
        }), "queue the actual W press");
    else if (button->isVisible())
        require (juce::MessageManager::callAsync ([button] { button->onClick(); }), "queue the actual A/B press");
    else pressComparisonMenuEntry (*editor, "A/B", require);
    require (afterQueuedMessages (choose), "choose PRE2 in the product menu");
    require (afterQueuedMessages ([&]
    {
        require (! post.candidateCompareActive(), "selecting PRE2 alone does not start replacement");
        choose();
    }), "confirm the placement in the product menu");
    require (afterQueuedMessages ([] {}), "deliver explicit A/B start");
    if (completeReturn) completeReturn (*editor);
    if (!post.candidateCompareActive())
    {
        std::cerr << "candidate start diagnostic: returning=" << post.candidateReturning()
            << " isolated=" << post.candidateMeasurementIsolated() << " finishing=" << post.liveCompareStatus().finishing
            << " admission=" << int (post.liveCompareAdmission (false)) << " blind=" << int (post.liveBlindStatus().stage)
            << " source=" << post.liveComparePreInstance() << '\n';
        if (const auto* view = candidateView (*editor))
            std::cerr << "candidate final feedback: " << view->feedback() << '\n';
    }
    require (post.candidateCompareActive(), "confirmed product button starts A/B");
    if (throughKey) keysWindow.clearContentComponent();
    post.editorBeingDeleted (editor.get()); editor.reset();
    require (post.candidateCompareActive(), "closing the editor keeps this explicit comparison");
}

template<class Require> void verifyCandidateSurfaces (Require require)
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        observatory::View view (observatory::Role::post); view.setVisible (true);
        observatory::LiveCompareFooter footer;
        footer.entryEnabled = footer.blindAvailable = footer.active = footer.candidate = true;
        footer.matched = true; footer.preGainTenthsDb = -60;
        view.setLiveCompareFooter (footer);
        for (const auto size : observatory::sizePresets)
        {
            view.setSize (size.width, size.height);
            for (const auto gain : { -240, -60, 0, 60, 240 })
                for (const auto held : { -240, -60, 0 })
                    for (const bool matched : { false, true })
                    {
                        footer.preGainTenthsDb = gain; footer.postHeldTenthsDb = held; footer.matched = matched;
                        view.setLiveCompareFooter (footer);
                        const auto* a = candidateControl (view, "observatory-live-pre");
                        const auto* b = candidateControl (view, "observatory-live-post");
                        if (a && b && (!a->isVisible() || !b->isVisible() || a->getWidth() != b->getWidth()))
                            std::cerr << "A/B footer: " << size.width << "x" << size.height << " gain=" << gain
                                << " held=" << held << " matched=" << matched << " A=" << a->getWidth()
                                << "/" << a->isVisible() << " B=" << b->getWidth() << "/" << b->isVisible() << '\n';
                        require (a && b && a->isVisible() && b->isVisible() && a->getWidth() == b->getWidth(),
                                 "A and B stay visible and exactly equal in width at every size and gain boundary");
                        for (const auto* id : { "observatory-live-pre", "observatory-live-post", "observatory-live-end" })
                        {
                            auto* button = dynamic_cast<juce::Button*> (candidateControl (view, id));
                            require (button && button->isVisible(), "candidate controls use the common LIVE surface");
                            const auto font = labelFont (view.presentationContext(), typography::TextRole::action);
                            require (text_style::shownWidth (font, button->getButtonText()) + 6.0f <= button->getWidth(),
                                     "candidate text fits every visible size and gain boundary");
                            require (!button->getButtonText().contains ("PRE") && !button->getButtonText().contains ("POST"),
                                     "candidate controls name A and B");
                        }
                    }
            footer.preGainTenthsDb = -60; footer.postHeldTenthsDb = 0; footer.matched = true;
            view.setLiveCompareFooter (footer);
            const auto root = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_CANDIDATE_PREVIEW", {});
            if (root.isNotEmpty())
            {
                const auto file = juce::File (root).getChildFile ("ab-" + juce::String (int (language)) + "-" + juce::String (size.width) + ".png");
                auto stream = file.createOutputStream();
                require (stream && stream->setPosition (0) && stream->truncate().wasOk()
                    && juce::PNGImageFormat().writeImageToStream (view.createComponentSnapshot (view.getLocalBounds()), *stream),
                    "candidate preview is rendered");
            }
        }
        footer.active = footer.candidate = false;
        view.setLiveCompareFooter (footer);
        const auto* blind = candidateControl (view, "observatory-local-blind");
        require (blind && blind->getTitle() == "PRE / POST Blind Compare",
                 "END restores the ordinary BLIND accessibility name");
        struct Choice { juce::String name, instanceId; bool hasName; juce::String hostName; int chainPosition = -1; };
        const std::array<Choice, 3> choices {{ { "Tone", "12345678-rest", true, {}, -1 },
            { "Tone", "87654321-rest", true, {}, -1 }, { {}, "abcdefgh-rest", false, {}, -1 } }};
        require (hypha::pair_labels::menuLabel (choices, choices[0]) == "Tone / 12345678"
            && hypha::pair_labels::menuLabel (choices, choices[1]) == "Tone / 87654321"
            && hypha::pair_labels::menuLabel (choices, choices[2]) == "PRE abcdefgh",
                 "duplicate and unnamed PREs retain their short identities");
        live_compare::LiveBlindStatus progress;
        progress.candidate = true; progress.stage = live_compare::BlindStage::preparing;
        progress.matchSecondsRemaining = 5;
        require (live_blind_ui::screenFor (progress, true, 1.0f).instruction == "MATCH: 5 s remaining",
                 "BLIND shows the measured MATCH countdown");
        for (const auto stage : { live_compare::BlindStage::active, live_compare::BlindStage::approval })
            for (const float level : { 1.0f, 0.5f })
            {
                live_compare::LiveBlindStatus status;
                status.candidate = true; status.stage = stage; status.lowerPostDb = -6.0;
                const auto screen = live_blind_ui::screenFor (status, true, level);
                blind_ui::ScreenComponent surface ("candidate-localized");
                surface.setScreen (screen); surface.setSize (900, 600);
                auto* detail = dynamic_cast<juce::Label*> (candidateControl (surface, "candidate-localized-text-3"));
                require (detail != nullptr, "actual BLIND detail label exists");
                const auto shown = text_style::shownText (detail->getText());
                const auto expected = language == i18n::Language::english ? screen.detail
                    : juce::String::fromUTF8 (stage == live_compare::BlindStage::approval
                        ? u8"6.0 dB下げます。終了時に同じ量だけ上がり、A→Bの接続に戻ります"
                        : level == 1.0f ? u8"終了すると通常の音量とA→Bの接続に戻ります"
                        : u8"終了すると音量が6.0 dB上がり、A→Bの接続に戻ります");
                require (shown == expected, "actual BLIND detail translates the complete level and chain explanation");
                auto* approve = candidateControl (surface, "candidate-localized-approve");
                require (approve && text_style::shownText (approve->getDescription()) == expected,
                         "the approval description uses the same complete translation");
            }
        for (const bool firstA : { false, true })
        {
            live_compare::LiveBlindStatus status;
            status.candidate = true; status.stage = live_compare::BlindStage::active;
            status.trial.firstPre = firstA; status.trial.played = 3;
            const auto anonymous = live_blind_ui::screenFor (status, true, 1.0f);
            require (anonymous.detail.contains ("A to B chain") && anonymous.endTitle == "End A/B and restore the chain",
                     "candidate BLIND explains the restored connection");
            require (anonymous.sourceOne == "SOURCE 1" && anonymous.sourceTwo == "SOURCE 2",
                     "neither assignment leaks into anonymous source labels");
            status.trial.revealed = true;
            const auto revealed = live_blind_ui::screenFor (status, true, 1.0f);
            require (revealed.sourceOne == (firstA ? "1: A" : "1: B")
                && revealed.sourceTwo == (firstA ? "2: B" : "2: A"), "REVEAL names A and B");
        }
    }
}
}
