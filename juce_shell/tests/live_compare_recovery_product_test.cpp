#include <string>
#include "ProductTestClock.h"
#include "LiveTimingFixtureAccess.h"
#include "EditorProductChecks.h"
#include "ReferenceValidationStorageSandbox.h"
#include "LiveCompareNoticeProductChecks.h"
#include "ShortcutListenCheck.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaReferenceAccessPanel.h"
#include "../src/chain_trace/ChainTraceScope.h"
#include "TestProcessorStorage.h"
#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

namespace hypha::tests::editor_product
{
struct Tick
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (Tick);
};
template struct PrivateAccess<Tick, &KirinHyphaEditor::timerCallback>;
struct LayoutReference
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (LayoutReference);
};
template struct PrivateAccess<LayoutReference, &KirinHyphaEditor::layoutReferenceAudition>;

struct OpenMatchChoice
{
    using Type = void (KirinHyphaEditor::*) (const live_compare::MatchPlan&);
    friend Type privateMember (OpenMatchChoice);
};
template struct PrivateAccess<OpenMatchChoice, &KirinHyphaEditor::chooseLiveCompareMatch>;
struct MatchChoiceMember
{
    using Type = live_compare_ui::MatchChoicePopup KirinHyphaEditor::*;
    friend Type privateMember (MatchChoiceMember);
};
template struct PrivateAccess<MatchChoiceMember, &KirinHyphaEditor::matchChoicePopup>;
struct MatchShortcut
{
    using Type = void (KirinHyphaEditor::*) (shortcuts::Command);
    friend Type privateMember (MatchShortcut);
};
template struct PrivateAccess<MatchShortcut, &KirinHyphaEditor::performShortcut>;

static void verifyMatchChoiceControls()
{
    NoticeRig rig;
    rig.selectPair(); rig.advance (0.2);
    require (rig.post->startLiveCompare() == live_compare::StartResult::started, "LISTEN starts for MATCH choice");
    rig.expectOrdinaryAudio = false;
    rig.advance (0.3);
    live_compare::MatchPlan plan;
    plan.needsApproval = true; plan.neededPreGainDb = 7.0; plan.limitedPreGainDb = 3.0; plan.lowerPostGainDb = -7.0;
    auto& popup = rig.editor.get()->*privateMember (MatchChoiceMember {});
    const auto open = [&] { (rig.editor.get()->*privateMember (OpenMatchChoice {})) (plan); };
    const auto button = [&] (const char* id)
    {
        auto* found = dynamic_cast<juce::Button*> (rig.view().findChildWithID (id));
        require (found != nullptr && found->onClick != nullptr, "the shipping comparison control exists");
        return found;
    };
    auto* pre = button ("observatory-live-pre");
    auto* end = button ("observatory-live-end");
    auto* listen = button ("observatory-live-compare");
    auto* match = button ("observatory-live-match");
    for (const bool keyboard : { false, true })
    {
        if (keyboard) (rig.editor.get()->*privateMember (MatchShortcut {})) (shortcuts::Command::matchListen);
        else match->onClick();
        open(); // deliver the same measured choice to the editor without a desktop window
        auto* focus = juce::Component::getCurrentlyFocusedComponent();
        require (popup.isVisible() && rig.editor->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey))
                     && ! popup.isVisible(), "Esc closes MATCH after button and shortcut requests");
        require (juce::Component::getCurrentlyFocusedComponent() == focus, "Esc leaves the existing focus in place");
    }
    for (const int width : { 300, 375, 450, 600, 750, 900 })
    {
        rig.editor->setSize (width, width * 2 / 3);
        rig.advance (0.05);
        open();
        auto* parent = popup.getParentComponent();
        require (parent != nullptr && parent->getLocalBounds().contains (popup.getBounds()),
                 "the shipping editor contains the MATCH choice at each size");
        const std::array<juce::Component*, 4> actions { pre, end, listen, &rig.view().operationsMenuAnchor() };
        for (auto* action : actions)
            if (action->isVisible())
            {
                const auto bounds = parent->getLocalArea (action, action->getLocalBounds());
                if (popup.getBounds().intersects (bounds))
                    std::cerr << "choice width=" << width << " panel=" << popup.getBounds().toString()
                              << " action=" << action->getComponentID() << " " << bounds.toString() << '\n';
                require (! popup.getBounds().intersects (bounds),
                         "the MATCH choice never covers a visible comparison control");
            }
        popup.close();
    }
    rig.editor->setSize (900, 600); rig.advance (0.1);
    open();
    require (popup.isVisible() && ! pre->isCurrentlyBlockedByAnotherModalComponent(), "MATCH leaves PRE usable");
    pre->onClick();
    require (! popup.isVisible() && rig.post->liveCompareStatus().preSelected, "PRE closes the choice and selects PRE");
    open();
    require (popup.isVisible() && ! end->isCurrentlyBlockedByAnotherModalComponent(), "MATCH leaves END usable");
    end->onClick();
    require (! popup.isVisible(), "END closes the choice");
    rig.advance (0.4);
    require (! rig.post->liveCompareStatus().active, "END actually finishes LISTEN");
    open();
    listen->onClick();
    require (! popup.isVisible() && rig.post->liveCompareStatus().active, "LISTEN closes the choice and starts again");
}

static void verifySelectionTrace (const juce::File& sandbox)
{
    NoticeRig rig;
    rig.selectPair(); rig.advance (0.2);
    require (rig.post->startLiveCompare() == live_compare::StartResult::started, "LISTEN starts for trace");
    rig.expectOrdinaryAudio = false; rig.advance (0.5);
    const auto folder = sandbox.getChildFile ("selection-trace");
    require (folder.createDirectory().wasOk(), "trace uses the disposable fixture folder");
    const auto previous = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_CHAIN_TRACE_DIR", {});
   #if JUCE_WINDOWS
    _putenv_s ("KIRIN_HYPHA_CHAIN_TRACE_DIR", folder.getFullPathName().toRawUTF8());
   #else
    setenv ("KIRIN_HYPHA_CHAIN_TRACE_DIR", folder.getFullPathName().toRawUTF8(), 1);
   #endif
    auto recorder = chain_trace::Recorder::fromEnvironment();
   #if JUCE_WINDOWS
    _putenv_s ("KIRIN_HYPHA_CHAIN_TRACE_DIR", previous.toRawUTF8());
   #else
    setenv ("KIRIN_HYPHA_CHAIN_TRACE_DIR", previous.toRawUTF8(), 1);
   #endif
    require (recorder != nullptr, "the trace recorder is armed");
    LiveTimingFixtureAccess::traceForTest (*rig.post, std::move (recorder));
    for (const bool pre : { false, true, false })
    {
        rig.post->selectLiveComparePre (pre);
        rig.block();
    }
    rig.bypassed = true;
    rig.post->selectLiveComparePre (true); rig.block();
    rig.bypassed = false; rig.post->setNonRealtime (true);
    rig.post->selectLiveComparePre (false); rig.block();
    rig.post->setNonRealtime (false);
    rig.post->selectLiveComparePre (true);
    {
        auto& state = rig.post.get()->*privateMember (NoticeLiveState {});
        const HostProcessClock clock;
        chain_trace::Scope scope (state, rig.postAudio.getArrayOfReadPointers(), 2, 512, 48000, true, 0,
                                  clock, false, false);
        LiveTimingFixtureAccess::processAaxAnalysisWithOtherOutputOwner (*rig.post, rig.postAudio);
    }
    rig.post->selectLiveComparePre (false); rig.block();
    LiveTimingFixtureAccess::traceForTest (*rig.post, {});
    const auto files = folder.findChildFiles (juce::File::findFiles, true, "blocks.jsonl");
    require (files.size() == 1, "the trace was flushed");
    std::vector<std::uint64_t> commands;
    int bypassRows = 0, offlineRows = 0, takenRows = 0;
    const auto number = [] (const juce::var& value) { return static_cast<std::uint64_t> (value.toString().getLargeIntValue()); };
    for (const auto& line : juce::StringArray::fromLines (files[0].loadFileAsString()))
    {
        const auto row = juce::JSON::parse (line);
        if (! row.hasProperty ("selectionSeen") || ! static_cast<bool> (row["selectionSeen"])) continue;
        const bool blocked = static_cast<bool> (row["bypassed"]) || static_cast<bool> (row["offline"])
                          || static_cast<bool> (row["outputTaken"]);
        bypassRows += static_cast<bool> (row["bypassed"]) ? 1 : 0;
        offlineRows += static_cast<bool> (row["offline"]) ? 1 : 0;
        takenRows += static_cast<bool> (row["outputTaken"]) ? 1 : 0;
        require (static_cast<bool> (row["selectionAccepted"]) == ! blocked,
                 "only a selection whose output is available is accepted");
        const auto entry = number (row["entryNanos"]), observed = number (row["selectionReadNanos"]);
        const auto accepted = number (row["selectionAcceptedNanos"]), exit = number (row["exitNanos"]);
        require (entry > 0 && entry <= observed && observed <= exit
                     && (blocked ? accepted == 0 : observed <= accepted && accepted <= exit),
                 "read and eligible acceptance are timestamped inside their own audio callback");
        commands.push_back (number (row["selectionCommand"]));
    }
    require (commands.size() == 7 && (commands[0] & 1u) == 0 && (commands[1] & 1u) != 0 && (commands[2] & 1u) == 0
                 && (commands[0] >> 32) < (commands[1] >> 32) && (commands[1] >> 32) < (commands[2] >> 32),
             "each accepted selection keeps its own callback and command serial");
    require (bypassRows == 1 && offlineRows == 1 && takenRows == 1, "each blocked callback was traced");
}

struct GeometryCounter final : juce::ComponentListener
{
    int changes = 0;
    void componentMovedOrResized (juce::Component&, bool, bool) override { ++changes; }
};

struct Clock final : juce::AudioPlayHead
{
    bool playing = true;
    std::int64_t position = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing); info.setTimeInSamples (position);
        return info;
    }
};

static void verifyRecoveryGeometry()
{
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        Processor processor (Processor::Role::Post);
        hypha::tests::prepareFixtureProcessor (processor, 48000, 512);
        std::unique_ptr<KirinHyphaEditor> editor (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
        editor->setVisible (true);
        auto* view = component<observatory::View> (*editor);
        auto* reference = component<reference_ui::Component> (*editor);
        auto* access = component<reference_ui::AccessPanel> (*editor);
        auto* strip = component<FeedbackStrip> (*editor);
        require (view && reference && access && strip, "shipping recovery surfaces exist");
        for (const auto preset : observatory::sizePresets)
        for (auto domain : { observatory::Domain::reference, observatory::Domain::frequency,
                             observatory::Domain::time, observatory::Domain::space, observatory::Domain::level })
        {
            editor->setSize (preset.width, preset.height);
            view->onDomainChange (domain);
            (editor.get()->*privateMember (ShowToast {})) ({});
            const auto full = view->analysisBodyBounds();
            (editor.get()->*privateMember (ShowToast {})) ("Timing changed: stop/play DAW (POST)");
            require (reference->getBounds() == view->analysisBodyBounds()
                && access->getBounds() == view->analysisBodyBounds(), "both Reference bodies follow feedback immediately");
            if (domain == observatory::Domain::reference)
            {
                require (! reference->getBounds().intersects (view->statusStripBounds()), "Reference never overlaps feedback");
                for (const bool owned : { false, true })
                {
                    auto state = reference->state();
                    state.osAccess = owned ? os_access::State::ownedDisconnected : os_access::State::unowned;
                    reference->setState (state);
                    (editor.get()->*privateMember (LayoutReference {}))();
                    require (reference->isVisible() == owned && access->isVisible() != owned,
                             "both real Reference surfaces exercised");
                    GeometryCounter counter;
                    reference->addComponentListener (&counter); access->addComponentListener (&counter);
                    strip->addComponentListener (&counter);
                    for (int tick = 0; tick < 100; ++tick)
                        (editor.get()->*privateMember (LayoutReference {}))();
                    require (counter.changes == 0, "unchanged presentation performs zero geometry changes");
                    reference->removeComponentListener (&counter); access->removeComponentListener (&counter);
                    strip->removeComponentListener (&counter);
                    if (strip->isVisible())
                    {
                        const auto point = editor->getLocalPoint (strip, strip->getLocalBounds().getCentre());
                        require (editor->getComponentAt (point) == strip, "feedback remains above real REF/access after refresh");
                    }
                }
                writeReview (*editor, "recovery-ref-" + juce::String (static_cast<int> (language))
                                          + "-" + juce::String (preset.width));
            }
            (editor.get()->*privateMember (Tick {}))();
            require (reference->getBounds() == view->analysisBodyBounds(), "real editor tick never restores stale bounds");
            (editor.get()->*privateMember (ShowToast {})) ({});
            require (view->analysisBodyBounds() == full, "clearing feedback restores the body without resizing the editor");
        }
        processor.editorBeingDeleted (editor.get()); editor.reset(); processor.releaseResources();
    }
}

static void verifyHoldAfterEnd()
{
    hypha::tests::ProductTestClock wallClock;
    Processor processor (Processor::Role::Post);
    hypha::tests::prepareFixtureProcessor (processor, 48000, 512);
    Clock clock;
    processor.setPlayHead (&clock);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    const auto process = [&]
    { buffer.clear(); processor.processBlock (buffer, midi); clock.position += 512; wallClock.advanceBlock (512, 48000); processor.serviceLiveCompare(); };
    LiveTimingFixtureAccess::noticeContent (processor, -3000);
    processor.finishLiveCompare(); process();
    using namespace live_compare_ui;
    require (processor.isPlaying() && ! processor.liveCompareStatus().finishing, "END acknowledged during playback");
    require (processor.liveCompareStatus().contentHeld
        && namedPresentation (processor.liveCompareStatus()).action == RecoveryAction::none,
        "after END a content timing notice asks for nothing");
    clock.playing = false; process();
    require (! processor.liveCompareStatus().contentHeld, "actual stopped callback clears the content notice");
    clock.playing = true; process();
    require (processor.liveBlindStatus().stage == live_compare::BlindStage::idle,
             "playback restart never resurrects the invalidated trial");
    require (processor.getLatencySamples() == 0 && buffer.getMagnitude (0, 512) == 0.0f,
             "recovery does not generate sound or latency");
    processor.releaseResources();
}

// Footer-only snapshots omit the feedback strip owned by the real editor. Exercise that
// composition too: the held/LOOP remedy must actually paint, not merely fit an unused rectangle.
static void verifyLoopPresentation()
{
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        Processor processor (Processor::Role::Post);
        hypha::tests::prepareFixtureProcessor (processor, 48000, 512);
        std::unique_ptr<KirinHyphaEditor> editor (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
        editor->setVisible (true);
        auto* view = component<observatory::View> (*editor);
        auto* strip = component<FeedbackStrip> (*editor);
        require (view && strip, "real editor owns the recovery strip");
        for (const auto preset : observatory::sizePresets)
        for (int scenario = 0; scenario < 5; ++scenario)
        {
            editor->setSize (preset.width, preset.height);
            live_compare::Status status;
            status.active = status.prolongedWait = true;
            status.matchHeld = scenario >= 2;
            status.matched = scenario == 1;
            status.preSelected = status.preWaiting = true;
            status.timingReentryPending = scenario == 3;
            status.interrupted = scenario == 4;
            if (status.interrupted) status.reason = live_compare::RecoveryReason::foreignRing;
            status.observation = scenario == 0 ? live_compare::RecoveryReason::loopUnproven
                : scenario == 1 ? live_compare::RecoveryReason::loopWaiting
                : scenario >= 3 ? live_compare::RecoveryReason::loopUnproven : live_compare::RecoveryReason::none;
            observatory::LiveCompareFooter footer;
            footer.active = footer.entryEnabled = true;
            footer.preSelected = status.preSelected; footer.preWaiting = status.preWaiting;
            footer.matched = status.matched || status.matchHeld; footer.matchHeld = status.matchHeld;
            view->setLiveCompareFooter (footer);
            (editor.get()->*privateMember (ShowToast {})) (live_compare_ui::namedRecovery (status));
            require (strip->isVisible() == view->statusStripFolded() && view->feedback().isNotEmpty(),
                     "LOOP/HELD remedy uses the visible strip or footer, according to its full width");
            require (strip->isVisible() || view->feedbackDetailsAnchor().isVisible(), "recovery text is not hidden");
            if (strip->isVisible())
            {
                const auto point = editor->getLocalPoint (strip, strip->getLocalBounds().getCentre());
                require (editor->getComponentAt (point) == strip, "LOOP/HELD remedy is not behind the analysis body");
            }
            writeReview (*editor, "loop-remedy-" + juce::String (static_cast<int> (language))
                + "-" + juce::String (scenario) + "-" + juce::String (preset.width));
        }
        processor.editorBeingDeleted (editor.get()); editor.reset(); processor.releaseResources();
    }
}

// A host that reports neither a continuous clock nor its output latency: LISTEN starts, POST plays,
// PRE waits, and the footer names the host instead of a stop/play that would not help. Once the same
// instance has seen a clock, a missing clock keeps its transient remedy.
static void verifyHostWithoutOutputLatency (const juce::File& sandbox)
{
    const auto storage = sandbox.getChildFile ("host-without-clock");
    require (storage.createDirectory(), "isolated preference storage for this host");
    ScopedChainTimingStorage preferences (storage);
    using namespace live_compare_ui;
    NoticeRig r;
    r.clock.continuous = r.clock.latency = false;
    r.advance (0.3); r.selectPair();
    require (r.post->startLiveCompare() == live_compare::StartResult::started, "LISTEN starts on a host without a clock");
    r.expectOrdinaryAudio = false;
    r.post->selectLiveComparePre (true); r.advance (0.6);
    const auto waiting = r.post->liveCompareStatus();
    require (waiting.active && waiting.preSelected && waiting.preWaiting && ! waiting.preAudible
        && waiting.observation == Reason::clockMissing && waiting.clockUnavailable,
        "PRE waits for a clock this host never supplies");
    require (juce::String (currentNamedPresentation (waiting).instruction).isEmpty() && r.view().feedback().isEmpty(),
             "a short wait on this host shows no guidance (2026-10-13 decision)");
    r.advance (2.7);
    const auto notice = currentNamedPresentation (r.post->liveCompareStatus());
    require (notice.action == RecoveryAction::noOutputLatency && r.view().feedback() == notice.instruction
        && r.editor->statusStory().joinIntoString (" ").contains ("PRE timing cannot be verified"),
        "the footer and the story name the host, not a restart");
    r.clock.playing = false; r.advance (0.2); r.clock.playing = true; r.advance (0.4);
    require (currentNamedPresentation (r.post->liveCompareStatus()).action == RecoveryAction::noOutputLatency,
             "stop/play on this host does not change the explanation");
    LiveTimingFixtureAccess::blindWaiting (*r.post, live_compare::BlindStage::preparing);
    auto blind = r.post->liveBlindStatus();
    blind.prolongedWait = true; // the same boundary once the wait passes 3 seconds
    require (blind.clockUnavailable && blindRecovery (blind, false).action == RecoveryAction::noOutputLatency,
             "Blind preparation on this host explains the same boundary");
    LiveTimingFixtureAccess::blindWaiting (*r.post, live_compare::BlindStage::idle);
    r.post->finishLiveCompare(); r.advance (0.3);
    require (! r.post->liveCompareStatus().active, "END still closes the session");
    // The same instance later sees a continuous clock: from then on a missing clock is transient.
    // A VST3 host looping a selection meanwhile never asks for loop playback off: that guidance
    // belongs to an uncertified AAX host alone.
    r.clock.continuous = true; r.advance (0.1);
    r.clock.continuous = false; r.clock.loopRange = true; r.advance (0.1);
    require (r.post->startLiveCompare() == live_compare::StartResult::started, "LISTEN starts again");
    r.post->selectLiveComparePre (true); r.advance (0.4);
    require (juce::String (currentNamedPresentation (r.post->liveCompareStatus()).instruction).isEmpty(),
             "a short transient clock wait shows no guidance");
    r.advance (2.8);
    const auto transient = r.post->liveCompareStatus();
    const auto remedy = currentNamedPresentation (transient);
    require (transient.active && ! transient.clockUnavailable && transient.loopBlock == hypha::live_compare::LoopPairingBlock::none
        && remedy.action != RecoveryAction::noOutputLatency && remedy.action != RecoveryAction::loopOff
        && juce::String (remedy.instruction).startsWith ("Clock missing"),
        "a host that has shown a clock keeps the transient clock remedy; VST3 loops ask for no loop change");
    r.post->finishLiveCompare(); r.advance (0.3);
}
}

// One CTest item per part keeps each item's real-time budget to one group of scenarios:
// presentation (geometry, hold after END, LOOP), notices (pair/restore/LISTEN/BLIND/END), host
// (a host without a clock). No argument runs every part for a local check.
int main (int argc, char** argv)
{
    const std::string part = argc > 1 ? argv[1] : "all";
    if (part != "all" && part != "presentation" && part != "notices" && part != "host" && part != "choice" && part != "trace")
    {
        std::cerr << "unknown recovery product part: " << part << '\n';
        return 2;
    }
    const auto runs = [&] (const char* name) { return part == "all" || part == name; };
    ReferenceValidationStorageSandbox sandbox;
    // macOS JUCE resolves the home without HOME; keep PRE display files out of the real Kirin OS.
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    hypha::i18n::holdLanguage (true);
    if (runs ("presentation"))
    {
        hypha::tests::editor_product::verifyRecoveryGeometry();
        hypha::tests::editor_product::verifyHoldAfterEnd();
        hypha::tests::editor_product::verifyLoopPresentation();
    }
    if (runs ("notices"))
    {
        hypha::tests::editor_product::verifyNoticeLifecycle (sandbox.directory());
        hypha::tests::editor_product::verifyListenFromKeys();
    }
    if (runs ("host")) hypha::tests::editor_product::verifyHostWithoutOutputLatency (sandbox.directory());
    if (runs ("choice")) hypha::tests::editor_product::verifyMatchChoiceControls();
    if (runs ("trace")) hypha::tests::editor_product::verifySelectionTrace (sandbox.directory());
    std::cout << "Recovery product (" << part << "): PASS (geometry, current recovery, actual pair/restore/LISTEN/END, bounded notices, host without a clock)\n";
}
