#pragma once
#include "LiveContentHoldUiTest.h"
#include "LiveTimingAvailabilityUiTest.h"
#include "TestTemporaryDirectory.h"
#include "../src/HyphaAttackV2Geometry.h"
#include "../src/HyphaComparisonPresentation.h"
#include "../src/HyphaHostAvailability.h"
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaReferenceComponent.h"
#include "../src/HyphaReferenceStatusModel.h"
#include "../src/HyphaReferenceRuntimeStatus.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaUiPreferences.h"
#include "../src/HyphaWidgets.h"
#include <cstdlib>
#include <iostream>
#include "TestPathBudget.h"
namespace hypha::tests::shell_repairs
{
inline void verifyPaint();
inline void require (bool condition, const char* message)
{
    if (! condition) { std::cerr << "Shell repairs: " << message << '\n'; std::exit (1); }
}
inline int preferenceWriter (int argc, char** argv)
{
    if (argc != 4 || juce::String (argv[1]) != "--preference-writer") return -1;
    const juce::File file (juce::String::fromUTF8 (argv[2]));
    for (int i = 0; i < 100; ++i)
        require (ui_preferences::write (file, argv[3], juce::String (i)), "child merges its preference");
    return 0;
}
inline void verifyPreferences()
{
    const auto folder = hypha::tests::temporaryParent()
        .getNonexistentChildFile ("pref", {}, false);
    require (folder.createDirectory(), "create isolated preference directory");
    const auto file = folder.getChildFile ("ui-preferences.txt");
    require (ui_preferences::write (file, "sentinel", "preserve"), "seed unrelated preference");
    juce::ChildProcess first, second;
    const auto arguments = [&] (const char* key) {
        return juce::StringArray { juce::File::getSpecialLocation (
            juce::File::currentExecutableFile).getFullPathName(), "--preference-writer", file.getFullPathName(), key }; };
    require (first.start (arguments ("language")) && second.start (arguments ("hover_help")), "start separate modules");
    const bool firstDone = first.waitForProcessToFinish (10000);
    const bool secondDone = second.waitForProcessToFinish (10000);
    if (! firstDone || ! secondDone || first.getExitCode() != 0 || second.getExitCode() != 0)
        std::cerr << "Preference children: " << first.getExitCode() << ", " << second.getExitCode()
                  << "\n" << first.readAllProcessOutput() << second.readAllProcessOutput();
    require (firstDone && secondDone && first.getExitCode() == 0 && second.getExitCode() == 0,
             "concurrent processes finish");
    require (ui_preferences::read (file, "language") == "99"
        && ui_preferences::read (file, "hover_help") == "99"
        && ui_preferences::read (file, "sentinel") == "preserve", "both keys and unrelated data survive concurrent writes");
    require (hypha::tests::removeFixtureDirectory (folder), "remove only disposable preferences");
}
inline void verify()
{
    hypha::tests::verifyLiveContentHoldUi (require);
    hypha::tests::verifyLiveTimingAvailabilityUi (require);
    live_compare::Status named;
    named.active = named.preSelected = named.preWaiting = named.timingUnavailable = true;
    require (juce::String (live_compare_ui::currentNamedPresentation (named).instruction).isEmpty(),
             "short timing loss has no preparation notice");
    named.prolongedWait = true;
    for (bool held : { false, true })
    {
        named.contentHeld = held;
        const auto notice = live_compare_ui::currentNamedPresentation (named);
        require (held ? notice.action == live_compare_ui::RecoveryAction::stopPlay
            : notice.action == live_compare_ui::RecoveryAction::none
                && juce::String (notice.instruction) == host_availability::timingText(),
            "long content loss explains delay; unavailable clocks do not promise progress");
    }
    named.contentHeld = false;
    named.active = false;
    require (juce::String (live_compare_ui::currentNamedPresentation (named).instruction).isEmpty(),
             "END removes a stale timing hold notice at normal POST level");
    named.postActual = named.postTarget = 0.5f;
    require (live_compare_ui::currentNamedPresentation (named).action == live_compare_ui::RecoveryAction::returnLevel,
             "ended unavailable comparison retains the safe explicit volume return");
    // A missing audio clock reads by host ability: a host that has shown a clock or an output-latency
    // report keeps the transient stop/play (or select PRE again) remedy; a host that never has is
    // told that PRE cannot be heard there, whether PRE waits or the selection was interrupted.
    live_compare::Status clock;
    clock.active = clock.preSelected = clock.preWaiting = clock.prolongedWait = true;
    clock.observation = live_compare::RecoveryReason::clockMissing;
    for (bool interrupted : { false, true })
    {
        clock.interrupted = interrupted;
        clock.reason = interrupted ? live_compare::RecoveryReason::clockMissing : live_compare::RecoveryReason::none;
        clock.clockUnavailable = false;
        const auto transient = live_compare_ui::currentNamedPresentation (clock);
        require (interrupted ? transient.action == live_compare_ui::RecoveryAction::selectPre
            : transient.action == live_compare_ui::RecoveryAction::stopPlay
                && juce::String (transient.instruction) == "Clock missing: restart playback (POST)",
            "a clock that was present keeps its transient remedy");
        clock.clockUnavailable = true;
        const auto absent = live_compare_ui::currentNamedPresentation (clock);
        const juce::String story = live_compare_ui::nextStep (absent.action);
        require (absent.action == live_compare_ui::RecoveryAction::noOutputLatency
            && juce::String (absent.instruction) == "This DAW has no verified audio clock; PRE is unavailable (POST)"
            && story.contains ("PRE timing cannot be verified") && ! story.containsIgnoreCase ("stop"),
            "a host without a clock or an output-latency report never promises PRE after stop/play");
    }
    for (auto stage : { live_compare::BlindStage::preparing, live_compare::BlindStage::approval,
                        live_compare::BlindStage::settling, live_compare::BlindStage::invalidated })
    {
        const bool stopped = stage == live_compare::BlindStage::invalidated;
        live_compare::LiveBlindStatus blind;
        blind.prolongedWait = true;
        blind.stage = stage; blind.observation = live_compare::RecoveryReason::clockMissing;
        if (stopped) blind.reason = live_compare::RecoveryReason::clockMissing;
        const auto transient = live_compare_ui::blindRecovery (blind, stopped);
        require (transient.action == (stopped ? live_compare_ui::RecoveryAction::endBlind
                                              : live_compare_ui::RecoveryAction::automatic),
                 "Blind on a host with a clock keeps waiting, or asks for END, then BLIND");
        blind.clockUnavailable = true;
        const auto absent = live_compare_ui::blindRecovery (blind, stopped);
        const auto screen = live_blind_ui::screenFor (blind, true, 1.0f);
        require (absent.action == live_compare_ui::RecoveryAction::noOutputLatency
            && juce::String (absent.instruction) == "This DAW has no verified audio clock; PRE is unavailable"
            && screen.guidanceShown && screen.recovery == absent.instruction
            && screen.approveShown == (stage == live_compare::BlindStage::approval)
            && screen.cause == live_compare_ui::cause (live_compare::RecoveryReason::clockMissing)
            && screen.instruction.isEmpty() && screen.endEnabled,
            "Blind on a host without a clock explains the host instead of waiting or retrying");
    }
    // An uncertified AAX host looping a range observes an unidentifiable loop as well, but loop
    // playback off compares: LISTEN, Blind and the PSB difference ask for that before any host
    // boundary; without the loop the same observation remains the host boundary.
    live_compare::Status looped;
    looped.active = looped.preSelected = looped.preWaiting = looped.prolongedWait = true;
    looped.observation = live_compare::RecoveryReason::loopClockUnavailable;
    looped.timingUnavailable = true;
    require (live_compare_ui::currentNamedPresentation (looped).action == live_compare_ui::RecoveryAction::none
        && juce::String (live_compare_ui::currentNamedPresentation (looped).instruction) == host_availability::timingText(),
        "a loop no host rule can identify is the host boundary where loop playback is not the cause");
    looped.loopBlock = hypha::live_compare::LoopPairingBlock::aaxLoop;
    for (bool hostClock : { false, true })
    {
        looped.clockUnavailable = hostClock; // never both on a real host; the actionable remedy comes first
        const auto notice = live_compare_ui::currentNamedPresentation (looped);
        require (notice.action == live_compare_ui::RecoveryAction::loopOff
            && juce::String (notice.instruction) == "Loop Playback on: turn it off to compare (POST)",
            "an uncertified AAX host looping a range asks for loop playback off before any host boundary");
    }
    for (auto stage : { live_compare::BlindStage::preparing, live_compare::BlindStage::settling })
    {
        live_compare::LiveBlindStatus blind;
        blind.prolongedWait = true;
        blind.stage = stage; blind.observation = live_compare::RecoveryReason::loopClockUnavailable;
        blind.timingUnavailable = true;
        blind.loopBlock = hypha::live_compare::LoopPairingBlock::aaxLoop;
        const auto guidance = live_compare_ui::blindRecovery (blind, false);
        const auto screen = live_blind_ui::screenFor (blind, true, 1.0f);
        require (guidance.action == live_compare_ui::RecoveryAction::loopOff
            && screen.recovery == "Turn off Loop Playback to compare"
            && screen.instruction.isEmpty()
            && screen.cause != host_availability::timingText() && screen.guidanceShown && screen.endEnabled,
            "Blind on an uncertified AAX host looping a range asks for loop playback off");
        blind.loopBlock = hypha::live_compare::LoopPairingBlock::none;
        require (live_blind_ui::screenFor (blind, true, 1.0f).cause
                == live_compare_ui::cause (live_compare::RecoveryReason::loopClockUnavailable)
            && live_blind_ui::screenFor (blind, true, 1.0f).recovery == "Timing unavailable; check PRE and play (POST)"
            && live_compare_ui::blindRecovery (blind, false).action == live_compare_ui::RecoveryAction::none,
            "without loop playback the same observation is the host boundary");
    }
    SpectrumComponent psb;
    psb.setAbsoluteObservation (false); psb.setSignalActive (true);
    require (psb.psbStatusText() == "PSB WARMING", "the PSB difference prepares while the pair proof is pending");
    psb.setPairingLoopBlock (hypha::live_compare::LoopPairingBlock::aaxLoop);
    require (psb.psbStatusText() == "Loop Playback on: turn it off to compare",
             "the PSB difference asks for loop playback off, as SHARP does");
    psb.setAbsoluteObservation (true);
    require (psb.psbStatusText() == "PSB WARMING", "the absolute PSB never asks for a loop change");
    psb.setAbsoluteObservation (false); psb.setPairingLoopBlock (hypha::live_compare::LoopPairingBlock::none);
    require (psb.psbStatusText() == "PSB WARMING", "loop playback off restores the ordinary preparation text");
    require (comparison_presentation::statusText (KIRIN_COMPARISON_STATE_REJECTED,
        KIRIN_COMPARISON_REASON_PRE_INACTIVE).contains ("CHECK PRE IS ENABLED"), "inactive PRE has an actionable reason");
    require (comparison_presentation::statusText (KIRIN_COMPARISON_STATE_REJECTED,
        KIRIN_COMPARISON_REASON_LOCAL_INACTIVE).contains ("COMPARISON PAUSED"), "stopped POST does not promise an update");
    const auto owner = juce::String ("Fixture (A/B)");
    require (i18n::translate ("ANALYSIS IN USE / " + owner, i18n::Language::japanese)
        == juce::String::fromUTF8 (u8"解析を使用中 ／ ") + owner, "Japanese prose uses one slash style without changing owner names");
    require (i18n::translate ("MID: analyze (L + R) / 2.", i18n::Language::japanese)
        .contains (juce::String::fromUTF8 (u8"（L + R） / 2")), "Japanese brackets and mathematical division remain distinct");
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        for (const int width : { 300, 375, 450, 525, 600, 750, 899, 900 })
        {
            const auto height = width * 2 / 3;
            const auto context = presentation::forEditor (width, height);
            // Two host boundaries: timing that cannot be verified, and a host without a clock or an
            // output-latency report. Each explains itself, allows END and fits at every size.
            for (bool hostClock : { false, true })
            for (bool playing : { false, true })
            {
                live_compare::LiveBlindStatus unavailable;
                unavailable.stage = live_compare::BlindStage::preparing;
                unavailable.timingUnavailable = ! hostClock;
                unavailable.clockUnavailable = hostClock;
                unavailable.observation = hostClock ? live_compare::RecoveryReason::clockMissing
                    : live_compare::RecoveryReason::loopClockUnavailable;
                require (!live_blind_ui::screenFor (unavailable, playing, 0.5f).guidanceShown,
                         "short host uncertainty is silent at every size");
                unavailable.prolongedWait = true;
                const auto screen = live_blind_ui::screenFor (unavailable, playing, 0.5f);
                require (screen.guidanceShown && screen.endEnabled && ! screen.sourcesShown
                    && screen.cause == live_compare_ui::cause (unavailable.observation)
                    && ! screen.recovery.containsIgnoreCase ("automatically")
                    && screen.recovery.contains ("verified audio clock") == hostClock,
                    "long host uncertainty explains its observed cause and allows END");
                live_blind_ui::Component view;
                view.setSize (width, height); view.setState (unavailable, playing, 0.5f);
                juce::Image image (juce::Image::ARGB, width, height, true);
                juce::Graphics g (image); view.paintEntireComponent (g, true);
                require (view.findChildWithID ("live-blind-end")->isEnabled(), "actual unavailable screen permits END");
                for (auto* child : view.getChildren())
                    if (child->isVisible()) require (view.getLocalBounds().contains (child->getBounds()), "unavailable guidance fits every size");
            }
            for (bool all : { false, true })
                for (int captureReduction : { 0, 32 })
                {
                    const auto bodyHeight = std::max (80, height - 125 - captureReduction);
                    const auto geometry = attack_v2::geometry (width, bodyHeight, context, all);
                    for (const auto& lane : geometry.lanes)
                    {
                        require (geometry.valueArea.contains (lane.metric) && geometry.valueArea.contains (lane.unit)
                            && geometry.valueArea.contains (lane.scope), "DRUM labels stay in their available body");
                        require (! lane.metric.intersects (lane.unit) && ! lane.scope.intersects (lane.reason), "DRUM label rows never overlap");
                        require (lane.value.getHeight() >= 12, "Capture and small DRUM bodies retain readable primary values");
                    }
                    for (size_t i = 0; i < geometry.lanes.size(); ++i)
                        for (size_t j = i + 1; j < geometry.lanes.size(); ++j)
                            require (! geometry.lanes[i].metric.intersects (geometry.lanes[j].metric)
                                && ! geometry.lanes[i].unit.intersects (geometry.lanes[j].metric), "adjacent DRUM lanes never overlap");
                }
        }
        require (i18n::tr (host_availability::timingText()) != juce::String()
            && i18n::tr (host_availability::outputLatencyText()) != juce::String(), "availability reasons exist in both languages");
    }
    for (int width : { 750, 899, 900 })
    {
        reference_ui::State state;
        state.separateComparisons = state.libraryReceived = true;
        state.blindLargeScreen = width >= 900;
        state.comparisonSlot = 3;
        state.readiness = reference_ui::Readiness::ready;
        state.osAccess = os_access::State::ready;
        state.songSets = { { "fixture-set", "References" } }; state.songSetId = "fixture-set";
        state.songs = { { "fixture-song", "Missing file" } }; state.songId = "fixture-song";
        reference_ui::SongFact fact; fact.prepared = true; fact.lufsI = -18;
        fact.preparation.reason = "source_unavailable"; fact.preparation.retry = "manual";
        state.songFacts = { fact }; state.aWindowLoudness = -14; state.aWindowBlocks = 100;
        reference_ui::Component panel;
        panel.setPresentationContext (presentation::forEditor (width, width * 2 / 3));
        panel.setSize (width, width * 2 / 3); panel.setState (state);
        auto* list = dynamic_cast<reference_ui::SongList*> (panel.findChildWithID ("reference-song-list"));
        require (list && list->isVisible() == (width >= 900), "750-899 px uses the same compact REF boundary as audition permission");
        if (list->isVisible()) require (list->rows().front().preparation == "NOT FOUND"
            && ! std::isfinite (list->rows().front().gainDb), "a missing song never offers READY or MATCH gain");
    }
    reference_audition::RuntimeSongPreparation failed;
    failed.reason = "analysis_failed"; failed.state = "pending";
    require (reference_ui::preparationFailed (failed)
        && reference_ui::preparationLine (failed).contains ("COULD NOT ANALYZE"), "Kirin OS analysis failure has its own remedy");
    reference_ui::State rejected;
    rejected.osAccess = os_access::State::ownedDisconnected;
    rejected.separateComparisons = true; rejected.comparisonSlot = 3; rejected.referenceStep = reference_ui::SourceStep::waitingForKirinOs;
    reference_audition::Snapshot runtime;
    runtime.state = reference_audition::RuntimeState::rejected;
    runtime.rejectionCode = "reference_library_rejected";
    rejected.status = reference_ui::runtimeStatus (runtime, rejected).status;
    rejected.readiness = reference_ui::Readiness::rejected;
    rejected.preparationOverdue = "REFERENCE NOT PUBLISHED YET / KEEP KIRIN OS OPEN";
    require (reference_ui::referenceStatusLine (rejected, false).text.contains ("FORMAT UNREADABLE"),
        "an unreadable Reference keeps its precise cause after the publication wait budget");
    juce::Component axHost;
    axHost.setSize (300, 200); axHost.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    axHost.setVisible (true);
    const auto attach = [&] (EditableName& child, int y) {
        axHost.addAndMakeVisible (child); child.setBounds (0, y, 240, 28); };
    EditableName name; attach (name, 0);
    require (name.getAccessibilityHandler() != nullptr
        && name.getAccessibilityHandler()->getTitle() == "PRE name", "PRE name has an accessibility label");
    {
        i18n::ScopedLanguage japanese (i18n::Language::japanese);
        require (name.getAccessibilityHandler()->getTitle() == juce::String::fromUTF8 (u8"PREの名前"),
                 "PRE name accessibility follows the chosen language");
    }
    require (name.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)) && name.isEditing(), "Return begins PRE name editing");
    EditableName space; attach (space, 28);
    require (space.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)) && space.isEditing(), "Space begins PRE name editing");
    EditableName locked; attach (locked, 56); locked.setEditingEnabled (false);
    locked.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    require (! locked.isEditing(), "locked names cannot edit through the keyboard");
    EditableName pressed; attach (pressed, 84);
    require (pressed.getAccessibilityHandler()->getActions().invoke (juce::AccessibilityActionType::press)
        && pressed.isEditing(), "AX press starts PRE name editing");
    EditableName post; attach (post, 112);
    int selected = 0; post.onSelect = [&] { ++selected; };
    post.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    require (selected == 1 && ! post.isEditing(), "POST pair selection keeps its existing action");
    verifyPaint();
    verifyPreferences();
    std::cout << "Shell repair UI: PASS (2 languages, 8 widths, transport boundaries, keyboard/AX, concurrent processes)\n";
}
}
#include "ShellRepairPaintChecks.h"
