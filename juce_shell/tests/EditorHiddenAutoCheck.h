#pragma once
#include "EditorProductChecks.h"
#include <cstring>
#include "TestProcessorStorage.h"
#include "../src/HyphaKeepPreWait.h"
namespace hypha::tests::editor_product
{
struct HiddenAutoAccess {};
void enableHiddenAuto (KirinHyphaEditor&, HiddenAutoAccess);
bool hiddenAutoOn (const KirinHyphaEditor&, HiddenAutoAccess);
bool hiddenAutoNotice (const KirinHyphaEditor&, HiddenAutoAccess);
double& hiddenAutoPeak (KirinHyphaEditor&, HiddenAutoAccess);
template <auto Member> struct HiddenAutoMember
{
    friend void enableHiddenAuto (KirinHyphaEditor& e, HiddenAutoAccess) { (e.*Member).on = true; }
    friend bool hiddenAutoOn (const KirinHyphaEditor& e, HiddenAutoAccess) { return (e.*Member).on; }
    friend bool hiddenAutoNotice (const KirinHyphaEditor& e, HiddenAutoAccess)
    { return (e.*Member).hiddenStopNoticePending; }
    friend double& hiddenAutoPeak (KirinHyphaEditor& e, HiddenAutoAccess) { return (e.*Member).peakDbtp; }
};
template struct HiddenAutoMember<&KirinHyphaEditor::liveCompareAuto>;
struct HiddenTimer
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (HiddenTimer);
};
template struct PrivateAccess<HiddenTimer, &KirinHyphaEditor::timerCallback>;
struct FollowWaiting
{
    using Type = void (KirinHyphaEditor::*) (const live_compare::Status&, double);
    friend Type privateMember (FollowWaiting);
};
template struct PrivateAccess<FollowWaiting, &KirinHyphaEditor::followLiveCompare>;
struct HiddenToast
{
    using Type = juce::String KirinHyphaEditor::*;
    friend Type privateMember (HiddenToast);
};
template struct PrivateAccess<HiddenToast, &KirinHyphaEditor::toastText>;
struct HiddenToastUntil
{
    using Type = double KirinHyphaEditor::*;
    friend Type privateMember (HiddenToastUntil);
};
template struct PrivateAccess<HiddenToastUntil, &KirinHyphaEditor::toastUntil>;
struct WaitKeepSince { using Type = double KirinHyphaEditor::*; friend Type privateMember (WaitKeepSince); };
template struct PrivateAccess<WaitKeepSince, &KirinHyphaEditor::keepPreWaitSince>;
struct WaitKeepNext { using Type = double KirinHyphaEditor::*; friend Type privateMember (WaitKeepNext); };
template struct PrivateAccess<WaitKeepNext, &KirinHyphaEditor::keepPreWaitNextAt>;
struct WaitKeepPair { using Type = juce::String KirinHyphaEditor::*; friend Type privateMember (WaitKeepPair); };
template struct PrivateAccess<WaitKeepPair, &KirinHyphaEditor::keepPreWaitPair>;
struct WaitListenUntil { using Type = double KirinHyphaEditor::*; friend Type privateMember (WaitListenUntil); };
template struct PrivateAccess<WaitListenUntil, &KirinHyphaEditor::liveCompareStartAfterReturnUntil>;
struct WaitListenBlind { using Type = bool KirinHyphaEditor::*; friend Type privateMember (WaitListenBlind); };
template struct PrivateAccess<WaitListenBlind, &KirinHyphaEditor::liveCompareStartAfterReturnBlind>;
struct WaitListenKey { using Type = juce::String KirinHyphaEditor::*; friend Type privateMember (WaitListenKey); };
template struct PrivateAccess<WaitListenKey, &KirinHyphaEditor::liveCompareStartAfterReturnKey>;
struct WaitMatchUntil { using Type = double KirinHyphaEditor::*; friend Type privateMember (WaitMatchUntil); };
template struct PrivateAccess<WaitMatchUntil, &KirinHyphaEditor::referenceRematchUntil>;
struct WaitAfterTail { using Type = live_compare_ui::AfterTailWait KirinHyphaEditor::*; friend Type privateMember (WaitAfterTail); };
template struct PrivateAccess<WaitAfterTail, &KirinHyphaEditor::afterTail>;
struct WaitMatchNext { using Type = double KirinHyphaEditor::*; friend Type privateMember (WaitMatchNext); };
template struct PrivateAccess<WaitMatchNext, &KirinHyphaEditor::referenceRematchNextAt>;
struct WaitMatchKey { using Type = juce::String KirinHyphaEditor::*; friend Type privateMember (WaitMatchKey); };
template struct PrivateAccess<WaitMatchKey, &KirinHyphaEditor::referenceRematchKey>;
struct WaitMenu { using Type = juce::PopupMenu (KirinHyphaEditor::*)(); friend Type privateMember (WaitMenu); };
template struct PrivateAccess<WaitMenu, &KirinHyphaEditor::operationsMenu>;
struct WaitView { using Type = observatory::View KirinHyphaEditor::*; friend Type privateMember (WaitView); };
template struct PrivateAccess<WaitView, &KirinHyphaEditor::observatoryView>;
struct WaitPairKey { using Type = juce::String (KirinHyphaEditor::*)() const; friend Type privateMember (WaitPairKey); };
template struct PrivateAccess<WaitPairKey, &KirinHyphaEditor::waitingPairKey>;

// A Keep, LISTEN or C MATCH press that waits runs only for the same target, within its deadline, and
// never after the window hid or a Stop: the real editor handlers check those before any side effect.
inline void verifyWaitingRequests()
{
    const juce::String changed = "All Keep targets changed or unavailable. Try again.";
    require (i18n::translate (changed, i18n::Language::english) == changed
             && i18n::translate (changed, i18n::Language::japanese) == juce::String::fromUTF8 (u8"All Keepの対象が変更・不明。再試行してください。"),
             "the cancelled All Keep explains its changed or missing targets in both languages");
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (Processor::Role::Post);
    hypha::tests::prepareFixtureProcessor (processor, 48'000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> owner (processor.createEditor());
    auto* e = dynamic_cast<KirinHyphaEditor*> (owner.get());
    require (e != nullptr, "shipping editor for waiting requests");
    e->setVisible (true);
    const auto now = [] { return juce::Time::getMillisecondCounterHiRes() * 0.001; };
    const auto tick = [&] { (e->*privateMember (HiddenTimer {}))(); };
    const auto pairKey = (e->*privateMember (WaitPairKey {})) ();  // the PRE, its name and the restore ticket
    // Keep: a changed pair drops the wait silently; a wait past its deadline says why and starts nothing.
    e->*privateMember (HiddenToast {}) = {};
    e->*privateMember (WaitKeepSince {}) = now(); e->*privateMember (WaitKeepNext {}) = 0.0;
    e->*privateMember (WaitKeepPair {}) = "another PRE|"; tick();
    require (e->*privateMember (WaitKeepSince {}) <= 0.0 && (e->*privateMember (HiddenToast {})).isEmpty(),
             "a Keep waiting for one PRE is dropped when the pair changes");
    // Only the two-minute bound asks to choose PRE again (output_ownership_test: 8 s still waits).
    e->*privateMember (WaitKeepSince {}) = now() - (keep_ui::preWaitSeconds + 1.0); e->*privateMember (WaitKeepNext {}) = 0.0;
    e->*privateMember (WaitKeepPair {}) = pairKey; tick();
    require (e->*privateMember (WaitKeepSince {}) <= 0.0
                 && e->*privateMember (HiddenToast {}) == "PRE unavailable. Choose PRE again.",
             "a Keep past its wait asks to choose PRE again before trying Keep");
    // A restore of the same pair (preset, Undo) while a Keep or LISTEN press waits drops the press: the
    // editor compares the processor's restore and pair generation, not only the PRE id and name.
    {
        juce::MemoryBlock same;
        processor.getStateInformation (same);
        const auto key = pairKey;
        e->*privateMember (HiddenToast {}) = {};
        e->*privateMember (WaitKeepSince {}) = now(); e->*privateMember (WaitKeepNext {}) = 0.0;
        e->*privateMember (WaitKeepPair {}) = key;
        e->*privateMember (WaitListenUntil {}) = now() + 5.0; e->*privateMember (WaitListenKey {}) = key;
        processor.setStateInformation (same.getData(), static_cast<int> (same.getSize()));
        tick();
        require (e->*privateMember (WaitKeepSince {}) <= 0.0 && e->*privateMember (WaitListenUntil {}) <= 0.0
                     && (e->*privateMember (HiddenToast {})).isEmpty() && ! processor.liveCompareStatus().active,
                 "a restore of the same pair drops a waiting Keep and LISTEN without starting either");
    }
    // While a Keep waits, MENU offers Stop, and the screen Stop drops the wait.
    e->*privateMember (WaitKeepSince {}) = now(); e->*privateMember (WaitKeepNext {}) = now() + 60.0;
    e->*privateMember (WaitKeepPair {}) = pairKey;
    if (processor.stereoWorkflowsSupported())
    {
        bool stopOffered = false;
        const auto menu = (e->*privateMember (WaitMenu {})) ();
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            stopOffered = stopOffered || it.getItem().itemID == 5;
        require (stopOffered, "a waiting Keep offers Stop in MENU");
    }
    auto& view = e->*privateMember (WaitView {});
    require (static_cast<bool> (view.onStop), "the screen Stop exists");
    view.onStop();
    require (e->*privateMember (WaitKeepSince {}) <= 0.0, "the screen Stop drops a waiting Keep");
    // V, C, B or Keep waiting for the A/B tail: another pair drops it without starting anything.
    {
        auto& waiting = e->*privateMember (WaitAfterTail {});
        waiting.action = live_compare_ui::AfterTail::referenceC; waiting.pairKey = "another PRE|"; tick();
        require ((e->*privateMember (WaitAfterTail {})).action == live_compare_ui::AfterTail::none
                     && ! processor.referenceAuditionSnapshot().bSelected,
                 "a press waiting for the A/B tail is dropped when the pair changes");
    }
    {
        auto& waiting = e->*privateMember (WaitAfterTail {});
        require (processor.pairedPreInstanceId().isEmpty(), "the re-prepared engine has not restored its pair yet");
        waiting = { live_compare_ui::AfterTail::keepAll, "saved PRE|", processor.restoreOrPairTicket(), now(), 0.0, true };
        tick();
        require (waiting.action == live_compare_ui::AfterTail::keepAll && !processor.isRecording(),
                 "a staged All Keep waits for its pair to be restored rather than being dropped");
        waiting.since = now() - (keep_ui::preWaitSeconds + 1.0); tick();
        require (waiting.action == live_compare_ui::AfterTail::none
                 && (e->*privateMember (HiddenToast {})) == "A/B tail not cleared: Keep not started",
                 "a missing pair does not extend the original two-minute bound");
    }
    {
        auto& waiting = e->*privateMember (WaitAfterTail {});
        waiting = { live_compare_ui::AfterTail::keepAll, "saved PRE|", processor.restoreOrPairTicket() + 1, now(), 0.0, true };
        e->*privateMember (HiddenToast {}) = {}; tick();
        require (waiting.action == live_compare_ui::AfterTail::none && (e->*privateMember (HiddenToast {})) == changed,
                 "a cancelled All Keep explains the changed authority");
    }
    for (const auto action : { live_compare_ui::AfterTail::keepPair, live_compare_ui::AfterTail::keepAll })
    {
        auto& waiting = e->*privateMember (WaitAfterTail {});
        waiting = { action, (e->*privateMember (WaitPairKey {})) (), processor.restoreOrPairTicket(),
                    now() - (keep_ui::preWaitSeconds + 1.0), now() + 60.0 };
        e->*privateMember (HiddenToast {}) = {}; tick();
        require (waiting.action == live_compare_ui::AfterTail::none && ! processor.isRecording()
                 && (e->*privateMember (HiddenToast {})) == "A/B tail not cleared: Keep not started",
                 "an expired tail wait ends before a throttled check can start Keep");
    }
    // LISTEN after RETURN and C MATCH: another target drops the press without starting or matching.
    e->*privateMember (HiddenToast {}) = {};
    e->*privateMember (WaitListenUntil {}) = now() + 5.0; e->*privateMember (WaitListenKey {}) = "another PRE|";
    e->*privateMember (WaitMatchUntil {}) = now() + 5.0; e->*privateMember (WaitMatchNext {}) = 0.0;
    e->*privateMember (WaitMatchKey {}) = "another C"; tick();
    require (e->*privateMember (WaitListenUntil {}) <= 0.0 && ! processor.liveCompareStatus().active,
             "a LISTEN press for another pair is dropped without starting LISTEN");
    require (e->*privateMember (WaitMatchUntil {}) <= 0.0 && (e->*privateMember (HiddenToast {})).isEmpty(),
             "a MATCH for another C is dropped without matching");
    // A waiting C MATCH has no deadline: it never asks for MATCH again. It ends only with its C
    // (no C sounds here, so it ends quietly).
    e->*privateMember (HiddenToast {}) = {};
    e->*privateMember (WaitMatchUntil {}) = 1.0; e->*privateMember (WaitMatchNext {}) = 0.0;
    e->*privateMember (WaitMatchKey {}) = {}; tick();
    require (e->*privateMember (WaitMatchUntil {}) <= 0.0 && (e->*privateMember (HiddenToast {})).isEmpty(),
             "a waiting C MATCH never asks for another press");
    e->*privateMember (WaitListenBlind {}) = true;
    e->*privateMember (WaitListenUntil {}) = now() + 5.0; e->*privateMember (WaitListenKey {}) = "another PRE|"; tick();
    require (e->*privateMember (WaitListenUntil {}) <= 0.0 && processor.liveBlindStatus().stage == live_compare::BlindStage::idle,
             "a BLIND press for another pair is dropped without starting BLIND");
    e->*privateMember (WaitListenBlind {}) = false;
    // Hiding the window drops every press that has not started.
    e->*privateMember (WaitKeepSince {}) = now(); e->*privateMember (WaitListenUntil {}) = now() + 5.0;
    e->*privateMember (WaitMatchUntil {}) = now() + 5.0;
    (e->*privateMember (WaitAfterTail {})).action = live_compare_ui::AfterTail::keepPair;
    e->setVisible (false); tick();
    require (e->*privateMember (WaitKeepSince {}) <= 0.0 && e->*privateMember (WaitListenUntil {}) <= 0.0
                 && e->*privateMember (WaitMatchUntil {}) <= 0.0
                 && (e->*privateMember (WaitAfterTail {})).action == live_compare_ui::AfterTail::none,
             "a hidden window drops every waiting press");
    processor.editorBeingDeleted (owner.get());
    owner.reset();
    processor.releaseResources();
}

// INV-LC16 with an actual LISTEN (the notice rig): a closed window's AUTO belongs to its LISTEN. A loop
// wrap and a playback gap while it is closed move sessionGeneration, not the LISTEN: the reopened
// window continues AUTO.
template <typename Rig> void verifyClosedAutoAcrossTimeline (Rig& r)
{
    require (hiddenAutoOn (*r.editor, {}), "AUTO is on as the window closes");
    const auto before = r.post->liveCompareStatus().sessionGeneration;
    r.close();
    r.clock.position = 0; r.advance (0.2);  // the host's loop returns to its start
    r.clock.playing = false; r.advance (0.05); r.clock.playing = true; r.advance (0.3);
    const auto closed = r.post->liveCompareStatus();
    require (closed.active && closed.sessionGeneration != before,
             "a loop wrap and a playback gap keep LISTEN and move sessionGeneration");
    r.open(); r.advance (0.03);
    require (hiddenAutoOn (*r.editor, {}) && ! hiddenAutoNotice (*r.editor, {}),
             "a reopened window continues AUTO across a loop wrap and a playback gap");
}

// END while the window is closed ends AUTO with the LISTEN; the reopened window says AUTO stopped
// instead of showing MATCH silently.
template <typename Rig> void verifyClosedAutoEnded (Rig& r)
{
    enableHiddenAuto (*r.editor, {});
    r.close(); r.post->finishLiveCompare(); r.advance (3.3);
    r.open(); r.advance (0.03);
    require (! hiddenAutoOn (*r.editor, {}) && r.view().feedback() == "AUTO stopped: editor hidden",
             "END while closed does not continue AUTO, and the reopened window says AUTO stopped");
    r.editor.get()->*privateMember (HiddenToastUntil {}) = 0.0; r.advance (0.03);
}

inline void verifyHiddenAutoStops()
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (Processor::Role::Post);
    hypha::tests::prepareFixtureProcessor (processor, 48'000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> owner (processor.createEditor());
    auto* editor = dynamic_cast<KirinHyphaEditor*> (owner.get());
    require (editor != nullptr, "shipping editor for hidden AUTO fixture");
    const auto before = processor.liveCompareStatus();
    auto waiting = before;
    waiting.active = true; waiting.matched = false; waiting.matchHeld = true;
    waiting.matchReady = false; waiting.verdict = live_compare::Verdict::calibrating;
    enableHiddenAuto (*editor, {});
    (editor->*privateMember (FollowWaiting {})) (waiting, 1000.0);
    require (hiddenAutoOn (*editor, {}) && processor.liveCompareStatus().gain == before.gain
             && processor.liveCompareStatus().postTarget == before.postTarget,
             "AUTO waits without moving gain until timing is verified");
    waiting.matchReady = true; waiting.verdict = live_compare::Verdict::accepted;
    waiting.observation = live_compare::RecoveryReason::clockMissing;
    (editor->*privateMember (FollowWaiting {})) (waiting, 1000.0);
    require (hiddenAutoOn (*editor, {}) && processor.liveCompareStatus().gain == before.gain
             && processor.liveCompareStatus().postTarget == before.postTarget,
             "an unverified observation cannot move AUTO gain");
    enableHiddenAuto (*editor, {});
    editor->setVisible (false);
    for (int n = 0; n < 5; ++n) (editor->*privateMember (HiddenTimer {}))();
    require (hiddenAutoOn (*editor, {}) && ! hiddenAutoNotice (*editor, {}),
             "a hidden window rests AUTO across hidden ticks without a stop notice");
    const auto after = processor.liveCompareStatus();
    require (std::memcmp (&after.gain, &before.gain, sizeof (after.gain)) == 0
             && std::memcmp (&after.postTarget, &before.postTarget, sizeof (after.postTarget)) == 0,
             "resting hidden AUTO preserves the approved PRE gain and POST attenuation");
    editor->*privateMember (HiddenToast {}) = "Capture could not be attached";
    editor->*privateMember (HiddenToastUntil {}) = juce::Time::getMillisecondCounterHiRes() * 0.001 + 3.0;
    editor->setVisible (true);
    (editor->*privateMember (HiddenTimer {}))();
    require (! hiddenAutoNotice (*editor, {})
             && editor->*privateMember (HiddenToast {}) == "Capture could not be attached",
             "a notice with no actual LISTEN session is discarded without overwriting Capture failure");
    editor->*privateMember (HiddenToastUntil {}) = 0.0;
    (editor->*privateMember (HiddenTimer {}))();
    require (! hiddenAutoOn (*editor, {}) && ! hiddenAutoNotice (*editor, {})
             && (editor->*privateMember (HiddenToast {})).isEmpty(),
             "an inactive session ends AUTO without a notice");
    editor->setVisible (false);
    (editor->*privateMember (HiddenTimer {}))();
    require (! hiddenAutoNotice (*editor, {}), "hiding without AUTO adds no stop notice");
    processor.editorBeingDeleted (owner.get());
    owner.reset();
    processor.releaseResources();
    verifyWaitingRequests();
}

}
