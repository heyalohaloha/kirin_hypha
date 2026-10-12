#pragma once
#include "EditorProductChecks.h"
#include "LiveTimingFixtureAccess.h"
#include "../src/HyphaLiveCompareRecoveryText.h"

namespace hypha::tests::editor_product
{
struct ComparisonNotice
{
    using Type = void (KirinHyphaEditor::*) (const KirinObservatoryFrame&);
    friend Type privateMember (ComparisonNotice);
};
template struct PrivateAccess<ComparisonNotice, &KirinHyphaEditor::refreshComparisonNotice>;
struct FeedbackUpdate
{
    using Type = void (KirinHyphaEditor::*) (double, bool, const juce::String&);
    friend Type privateMember (FeedbackUpdate);
};
template struct PrivateAccess<FeedbackUpdate, &KirinHyphaEditor::updateFeedback>;

// Real editor arbitration, with independent measurement and listening observations. No PCM
// mapping or host connection is needed: this tests presentation, not timing acceptance.
inline void verifyReaperListenFeedback()
{
    using Reason = live_compare::RecoveryReason;
    int cases = 0, failures = 0;
    const auto check = [&] (bool ok, const char* why)
    { ++cases; if (!ok) { ++failures; std::cerr << "LISTEN feedback: " << why << '\n'; } };
    for (auto wrapper : { juce::AudioProcessor::wrapperType_VST3,
                         juce::AudioProcessor::wrapperType_AudioUnit, juce::AudioProcessor::wrapperType_AAX })
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
        Processor post (Processor::Role::Post);
        std::unique_ptr<KirinHyphaEditor> editor (dynamic_cast<KirinHyphaEditor*> (post.createEditorIfNeeded()));
        editor->setVisible (true);
        auto* view = component<observatory::View> (*editor);
        auto* strip = component<FeedbackStrip> (*editor);
        require (view && strip, "LISTEN editor surfaces exist");
        editor->setSize (300, 200);
        KirinObservatoryFrame frame {};
        frame.comparison_generation = 10;
        frame.comparison_state = KIRIN_COMPARISON_STATE_POST_ABSOLUTE;
        frame.comparison_reason = KIRIN_COMPARISON_REASON_PRE_INACTIVE;
        const auto deliver = [&] {
            view->onTargetChange (observatory::ObservationTarget::delta);
            (editor.get()->*privateMember (ComparisonNotice {})) (frame);
            ++frame.comparison_generation;
        };
        const auto update = [&] {
            const auto text = live_compare_ui::currentNamedPresentation (post.liveCompareStatus()).instruction;
            (editor.get()->*privateMember (FeedbackUpdate {}))
                (juce::Time::getMillisecondCounterHiRes() * 0.001, false, text);
            return juce::String (text);
        };
        // A stopped pair can report inactive before playback starts. Its queued toast must not
        // keep claiming PRE is stopped once LISTEN is measuring the first loop.
        deliver();
        check (view->feedback().contains ("PRE NOT RUNNING"), "inactive pair keeps its existing notice outside LISTEN");
        for (auto reason : { Reason::reaperFirstCycle, Reason::reaperDelayUnverified,
                             Reason::reaperLoopTooShort, Reason::calibrating, Reason::none,
                             Reason::notWritten })
        {
            LiveTimingFixtureAccess::timingObservation (post, reason,
                static_cast<std::uint8_t> (live_compare::ClockAuthority::certifiedProject));
            const auto expected = update();
            check (view->feedback() == expected, "queued measurement notice cannot override current LISTEN guidance");
            deliver(); update();
            check (view->feedback() == expected, "new inactive measurement cannot claim a live PRE stopped");
            text_style::ShownTextLog drawn;
            const auto image = strip->createComponentSnapshot (strip->getLocalBounds());
            check (image.isValid() && ! drawn.texts().joinIntoString (" ").contains (
                text_style::shownText (u8"PRE NOT RUNNING — CHECK PRE IS ENABLED")),
                "neither language paints the false stopped-PRE notice");
        }
        post.stopLiveCompare();
        (editor.get()->*privateMember (ComparisonNotice {})) (frame); update();
        check (view->feedback().isEmpty(), "END cannot revive an old pair-action inactive notice");
        frame.comparison_reason = KIRIN_COMPARISON_REASON_LAYOUT_MISMATCH;
        deliver(); update();
        check (view->feedback().contains ("CHANNEL LAYOUTS DIFFER"), "unrelated explicit-action errors still notify");
        (editor.get()->*privateMember (ShowToast {})) ("MATCH failed; try again");
        update();
        check (view->feedback() == "MATCH failed; try again", "direct listening actions retain toast priority");
        post.editorBeingDeleted (editor.get()); editor.reset();
    }
    std::cout << "LISTEN feedback: " << cases << " checks, " << failures << " failures\n";
    if (failures) throw std::runtime_error ("LISTEN feedback regression");
}
}
