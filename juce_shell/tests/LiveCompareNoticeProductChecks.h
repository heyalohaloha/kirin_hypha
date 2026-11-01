#pragma once
#include "TestProcessorStorage.h"
#include "EditorProductChecks.h"
#include "EditorHiddenAutoCheck.h"
#include "LiveTimingFixtureAccess.h"
#include "ProductTestClock.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include <chrono>
#include <cstring>
#include <thread>

namespace hypha::tests::editor_product
{
struct NoticeEditorTick { using Type = void (KirinHyphaEditor::*)(); friend Type privateMember (NoticeEditorTick); };
template struct PrivateAccess<NoticeEditorTick, &KirinHyphaEditor::timerCallback>;
struct NoticeEnableWrites { using Type = void (Processor::*)(); friend Type privateMember (NoticeEnableWrites); };
template struct PrivateAccess<NoticeEnableWrites, &Processor::enableWritesNow>;
struct NoticeProcessorTick { using Type = void (Processor::*)(); friend Type privateMember (NoticeProcessorTick); };
template struct PrivateAccess<NoticeProcessorTick, &Processor::timerCallback>;
struct NoticeFeedback
{
    using Type = void (KirinHyphaEditor::*) (double, bool, const juce::String&);
    friend Type privateMember (NoticeFeedback);
};
template struct PrivateAccess<NoticeFeedback, &KirinHyphaEditor::updateFeedback>;
struct NoticeAddMenu
{
    using Type = void (KirinHyphaEditor::*) (juce::PopupMenu&, bool);
    friend Type privateMember (NoticeAddMenu);
};
template struct PrivateAccess<NoticeAddMenu, &KirinHyphaEditor::addLiveCompareMenu>;
struct NoticeFollowChoice
{
    using Type = void (KirinHyphaEditor::*) (int);
    friend Type privateMember (NoticeFollowChoice);
};
template struct PrivateAccess<NoticeFollowChoice, &KirinHyphaEditor::handleLiveCompareFollowChoice>;
struct NoticeMatchChoice
{
    using Type = void (KirinHyphaEditor::*) (const live_compare::MatchPlan&, int);
    friend Type privateMember (NoticeMatchChoice);
};
template struct PrivateAccess<NoticeMatchChoice, &KirinHyphaEditor::handleLiveCompareMatchChoice>;
struct NoticeLiveState
{
    using Type = live_compare::ProcessorState Processor::*;
    friend Type privateMember (NoticeLiveState);
};
template struct PrivateAccess<NoticeLiveState, &Processor::liveCompare>;

inline void verifyCurrentRecoveryNotHistory()
{
    using namespace live_compare_ui;
    for (auto reason : { Reason::none, Reason::stopped, Reason::positionChanged, Reason::callbackGap,
        Reason::clockMissing, Reason::projectClockMissing, Reason::calibrating, Reason::writing,
        Reason::beforeRun, Reason::notWritten, Reason::overwritten, Reason::torn, Reason::foreignRing,
        Reason::pairChanged, Reason::preUnavailable, Reason::formatChanged, Reason::restored,
        Reason::compensationOff, Reason::contentChanged, Reason::bypassed, Reason::offline,
        Reason::outputTaken, Reason::gainChanged, Reason::nonFinite, Reason::ceiling,
        Reason::blockTooLarge, Reason::randomUnavailable, Reason::unknown,
        Reason::loopUnproven, Reason::loopWaiting, Reason::loopTooShort, Reason::loopClockUnavailable })
    for (auto admission : { live_compare::StartResult::started, live_compare::StartResult::noPair,
                            live_compare::StartResult::comparisonBusy })
    {
        live_compare::Status status;
        status.reason = status.observation = reason;
        status.interrupted = status.compensationOff = true;
        require (*currentNamedPresentation (status, admission).instruction == 0,
                 "inactive unity output has no current remedy, regardless of retained history/admission");
        for (const bool actual : { false, true })
        {
            status.postActual = actual ? 0.5f : 1.0f;
            status.postTarget = actual ? 1.0f : 0.5f;
            require (currentNamedPresentation (status, admission).action == RecoveryAction::returnLevel,
                     "current actual or target attenuation keeps reachable RETURN even without a reason");
        }
        status.postActual = status.postTarget = 1.0f;
        status.finishing = true;
        require (currentNamedPresentation (status, admission).action == RecoveryAction::busy,
                 "END waiting for an audio callback remains explicit");
        status.finishing = false; status.contentHeld = true;
        require (currentNamedPresentation (status, admission).action == RecoveryAction::none,
                 "after END at normal POST level a timing hold has no stop/play remedy; LISTEN shows it again");
        status.contentHeld = false; status.active = true;
        const auto current = currentNamedPresentation (status, admission);
        const auto recovery = namedPresentation (status, admission);
        require (current.reason == recovery.reason && current.action == recovery.action
            && juce::String (current.instruction) == recovery.instruction,
                 "active recovery preserves the existing typed safety remedy");
    }
}

struct NoticeClock final : juce::AudioPlayHead
{
    bool playing = true;
    // What this VST3 host supplies: its continuous sample clock and its output-latency report. Off,
    // the wrapper still names the sources, as it does for a host that gives neither value.
    bool continuous = true, latency = true;
    bool loopRange = false; // the host loops a selection (120 BPM at 48 kHz: a beat is 24000 samples)
    std::int64_t position = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing); info.setTimeInSamples (position);
        info.setKirinAuxiliaryClockSource (1);
        if (continuous) info.setKirinAuxiliaryClockSamples (position);
        info.setKirinPresentationLatencySource (1);
        if (latency) info.setKirinOutputPresentationLatencySamples (0);
        if (loopRange)
        {
            info.setIsLooping (true); info.setLoopPoints (LoopPoints { 0.0, 64.0 });
            info.setPpqPosition (static_cast<double> (position) / 24000.0); info.setBpm (120.0);
        }
        return info;
    }
};

struct NoticeRig
{
    ProductTestClock wallClock;
    NoticeClock clock;
    std::unique_ptr<Processor> pre, post;
    std::unique_ptr<KirinHyphaEditor> editor;
    juce::AudioBuffer<float> preAudio { 2, 512 }, postAudio { 2, 512 };
    juce::MidiBuffer midi;
    bool expectOrdinaryAudio = true, bypassed = false;
    float chainGain = 1.0f; // what the chain between PRE and POST does; another gain tells PRE from POST
    NoticeRig()
    {
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            auto p = std::make_unique<Processor> (role);
            auto layout = p->getBusesLayout();
            layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
            layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
            require (p->setBusesLayout (layout), "notice fixture uses actual stereo");
            p->setPlayHead (&clock); hypha::tests::prepareFixtureProcessor (*p, 48000, 512);
            require (LiveTimingFixtureAccess::configureStudioProClock (*p), "qualified fixture clock");
            (p.get()->*privateMember (NoticeEnableWrites {}))();
            (role == Processor::Role::Pre ? pre : post) = std::move (p);
        }
        open();
    }
    ~NoticeRig() { close(); pre->releaseResources(); post->releaseResources(); }
    void open()
    {
        editor.reset (dynamic_cast<KirinHyphaEditor*> (post->createEditorIfNeeded()));
        require (editor != nullptr, "notice test uses the real shipping editor");
        editor->setVisible (true); editor->setSize (900, 600);
    }
    void close() { if (editor) post->editorBeingDeleted (editor.get()); editor.reset(); }
    observatory::View& view() { return *component<observatory::View> (*editor); }
    void block()
    {
        for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 512; ++n)
        {
            const auto value = 0.05f * std::sin (float (clock.position + n) * 0.071f);
            preAudio.setSample (c, n, value); postAudio.setSample (c, n, chainGain * value);
        }
        pre->processBlock (preAudio, midi);
        // A processor with a bypass parameter is never sent processBlockBypassed by the JUCE
        // AU/VST3/AAX wrappers: the host sets the parameter and processBlock still runs.
        if (auto* bypass = post->getBypassParameter()) bypass->setValue (bypassed ? 1.0f : 0.0f);
        post->processBlock (postAudio, midi);
        wallClock.advanceBlock (512, 48000);
        if (expectOrdinaryAudio)
            for (int c = 0; c < 2; ++c)
                require (std::memcmp (preAudio.getReadPointer (c), postAudio.getReadPointer (c),
                                     512 * sizeof (float)) == 0, "ordinary A path remains bit identical");
        if (clock.playing) clock.position += 512;
        (pre.get()->*privateMember (NoticeProcessorTick {}))();
        (post.get()->*privateMember (NoticeProcessorTick {}))();
        if (editor) (editor.get()->*privateMember (NoticeEditorTick {}))();
    }
    void advance (double seconds)
    {
        // Keep wall time with audio time for the real worker/IO threads, without adding each
        // sleep's overshoot: a virtual CI machine or background QoS can stretch an 11 ms sleep to
        // ~100 ms. Sleep only while ahead of the audio; a late block is followed without a sleep.
        const auto blocks = static_cast<int> (std::ceil (seconds * 48000 / 512));
        const auto period = std::chrono::nanoseconds (512LL * 1'000'000'000LL / 48000);
        auto due = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i)
        {
            block();
            due += period;
            if (std::chrono::steady_clock::now() < due) std::this_thread::sleep_until (due);
        }
    }
    void selectPair()
    {
        auto preview = post->createPairPreview();
        require (pair_preview::request (preview), "actual PRE discovery request");
        // The PRE publishes on its own thread; a loaded runner can take seconds (10 s, as elsewhere).
        for (int n = 0; n < 500; ++n)
        {
            advance (0.02); KirinPairPreviewValue value {};
            if (kirin_hypha_pair_preview_poll (preview.get(), &value) && value.complete && value.has_single)
            { require (post->setPairCandidate (pre->instanceId(), "Fixture PRE"), "actual PRE selected"); return; }
        }
        require (false, "PRE discovery deadline");
    }
    void silentOrdinary()
    {
        const auto s = post->liveCompareStatus();
        require (! s.active && ! s.preAudible && ! s.finishing && s.postActual == 1.0f && s.postTarget == 1.0f,
                 "normal pairing/restoration does not start LISTEN or hold output");
        require (view().feedback().isEmpty(), "normal measurement has no stale listening notice");
        require (! editor->statusStory().joinIntoString (" ").startsWith ("LISTEN:"), "no old LISTEN story");
        juce::PopupMenu menu;
        (editor.get()->*privateMember (NoticeAddMenu {})) (menu, false);
        for (juce::PopupMenu::MenuItemIterator item (menu); item.next();)
            require (! item.getItem().text.contains ("PRE changed") && ! item.getItem().text.contains ("Reopened:"),
                     "MENU does not turn historical revocation into a current remedy");
    }
};

inline void verifyNoticeLifecycle (const juce::File& sandbox)
{
    ScopedChainTimingStorage preferences (sandbox);
    verifyCurrentRecoveryNotHistory();
    NoticeRig r;
    r.advance (0.3); r.silentOrdinary();
    r.selectPair(); r.advance (0.15); r.silentOrdinary(); // includes the first interruption edge
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        (r.editor.get()->*privateMember (SyncLanguage {})) (true);
        for (const auto size : observatory::sizePresets)
        for (auto domain : { observatory::Domain::level, observatory::Domain::time,
            observatory::Domain::frequency, observatory::Domain::space, observatory::Domain::reference })
        {
            r.editor->setSize (size.width, size.height); r.view().onDomainChange (domain);
            r.advance (0.03); r.silentOrdinary();
            if (domain == observatory::Domain::level)
                writeReview (*r.editor, "paired-measuring-" + juce::String (static_cast<int> (language))
                    + "-" + juce::String (size.width));
        }
        r.view().setManualHybridVuVisible (true); r.advance (0.03); r.silentOrdinary();
        r.view().setManualHybridVuVisible (false);
    }
    r.editor->setVisible (false); r.advance (0.1);
    r.editor->setVisible (true); r.advance (0.1); r.silentOrdinary();
    r.close(); r.open(); r.advance (0.1); r.silentOrdinary();
    r.expectOrdinaryAudio = false;
    require (r.post->startLiveCompare() == live_compare::StartResult::started, "hide test starts real LISTEN");
    r.post->selectLiveComparePre (true); r.advance (0.3);
    require (r.post->liveCompareStatus().preAudible, "PRE actually plays before hide");
    const auto status = r.post->liveCompareStatus();
    const auto oldGeneration = status.sessionGeneration + 1;
    enableHiddenAuto (*r.editor, {});
    (r.editor.get()->*privateMember (NoticeFollowChoice {})) (2);
    require (! hiddenAutoOn (*r.editor, {}), "Stop AUTO remains effective after the menu generation changes");
    auto& state = r.post.get()->*privateMember (NoticeLiveState {});
    (r.editor.get()->*privateMember (NoticeFollowChoice {})) (1);
    require (state.namedMatchPending, "MATCH again requests a fresh measurement after a changed menu generation");
    state.namedMatchPending = false;
    live_compare::MatchPlan stalePlan;
    stalePlan.generationBound = true;
    stalePlan.generation = oldGeneration;
    (r.editor.get()->*privateMember (NoticeMatchChoice {})) (stalePlan, 1);
    require (state.namedMatchPending && r.post->liveCompareStatus().postTarget == status.postTarget,
             "stale approval requests a fresh measurement without applying old attenuation");
    state.namedMatchPending = false;
    (r.editor.get()->*privateMember (NoticeMatchChoice {})) (stalePlan, 2);
    require (state.namedMatchPending && r.post->liveCompareStatus().gain == status.gain,
             "stale TP LIMIT requests a fresh measurement without applying old gain");
    state.namedMatchPending = false;
    r.editor->setVisible (false); r.advance (0.1);
    require (r.post->liveCompareStatus().active && r.post->liveCompareStatus().preSelected
        && r.post->liveCompareStatus().preAudible, "hide preserves LISTEN and verified output");
    r.editor->setVisible (true); r.advance (0.1);
    require (r.post->beginLiveBlind() == live_compare::StartResult::started, "hide test begins real BLIND");
    r.advance (4.0);
    require (r.post->liveBlindStatus().stage == live_compare::BlindStage::active, "real MATCH establishes BLIND");
    const auto trial = r.post->liveBlindStatus().trial;
    r.editor->setVisible (false); r.advance (0.1);
    const auto hidden = r.post->liveBlindStatus();
    require (hidden.stage == live_compare::BlindStage::active && hidden.trial.epoch == trial.epoch
        && hidden.trial.firstPre == trial.firstPre && hidden.trial.selected == trial.selected,
        "hide preserves the anonymous assignment and selection");
    r.editor->setVisible (true); r.advance (0.1);
    require (r.post->liveBlindStatus().trial.epoch == trial.epoch, "show preserves the same BLIND epoch");
    // The view keeps the assignment anonymous until REVEAL, so find the PRE source by listening.
    const auto selectAudiblePre = [&]
    {
        for (int source = 1; source <= 2; ++source)
        {
            require (r.post->selectLiveBlind (source), "bypass test selects an anonymous source");
            r.advance (0.2);
            for (int n = 0; n < 20 && ! r.post->liveCompareStatus().preAudible; ++n) r.advance (0.05);
            if (r.post->liveCompareStatus().preAudible) return true;
        }
        return false;
    };
    require (selectAudiblePre(), "one anonymous source actually plays PRE before bypass");
    // Every bypassed block is checked bit identical (POST) by advance(); the realtime path marks
    // the substituted PRE for REVEAL and keeps the same anonymous trial.
    r.bypassed = r.expectOrdinaryAudio = true; r.advance (0.1);
    require (r.post->liveBlindStatus().trial.usedPostFallback && ! r.post->liveCompareStatus().preAudible
        && r.post->liveBlindStatus().stage == live_compare::BlindStage::active
        && r.post->liveBlindStatus().trial.epoch == trial.epoch, "bypass retains the trial and records actual POST fallback");
    r.bypassed = r.expectOrdinaryAudio = false; r.advance (0.3);
    require (r.post->liveCompareStatus().preAudible && r.post->liveBlindStatus().trial.epoch == trial.epoch,
             "fresh timing after bypass automatically returns to the same anonymous PRE");
    r.post->finishLiveCompare(); r.advance (0.6); r.expectOrdinaryAudio = true;
    r.post->clearPairCandidate(); r.advance (0.15); r.silentOrdinary();
    r.selectPair(); r.advance (0.15); r.silentOrdinary();
    juce::MemoryBlock saved; r.post->getStateInformation (saved);
    r.post->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    r.advance (0.2); r.silentOrdinary();
    r.clock.playing = false; r.advance (0.2); r.silentOrdinary();
    r.clock.playing = true; r.advance (0.2); r.silentOrdinary();
    r.close(); r.open(); r.advance (0.1); r.silentOrdinary();
    // Explicit failures are visible once; measurement ticks and old reasons must not replay them.
    for (const auto* text : { "Capture saved", "Capture could not be attached", "Another Keep is active",
                            "Auto-stopped after 10 min idle. Take saved." })
    {
        (r.editor.get()->*privateMember (ShowToast {})) (text);
        require (r.view().feedback() == text, "operation result is immediately visible");
        const auto future = juce::Time::getMillisecondCounterHiRes() * 0.001 + 10.0;
        (r.editor.get()->*privateMember (NoticeFeedback {})) (future, false, {});
        r.advance (0.1); r.silentOrdinary();
    }
    (r.editor.get()->*privateMember (NoticeFeedback {})) (
        juce::Time::getMillisecondCounterHiRes() * 0.001 + 10.0, false, "Failed to start record");
    require (r.view().feedback() == "Failed to start record", "a real persistent fault is not expired as completion");
    r.advance (0.1); r.silentOrdinary();
    r.selectPair();
    require (r.post->startLiveCompare() == live_compare::StartResult::started, "explicit LISTEN still starts");
    r.expectOrdinaryAudio = false;
    r.advance (0.6); r.post->selectLiveComparePre (true); r.advance (1.0);
    require (r.post->liveCompareStatus().active && r.post->liveCompareStatus().preAudible, "verified PRE is audible");
    r.advance (3.2);
    // advance() feeds one 512-frame block per ~11 ms of wall time, so a slow runner has less audio
    // after the same seconds. Press MATCH once a real window can be measured (bounded wait).
    for (int n = 0; n < 100 && ! r.post->measureLiveCompare().ok(); ++n) r.advance (0.1);
    r.view().onLiveCompareMatch(); r.advance (0.2);
    for (int n = 0; n < 20 && ! r.post->liveCompareStatus().matched; ++n) r.advance (0.1);
    require (r.post->liveCompareStatus().matched, "actual MATCH qualifies the AUTO notice session");
    // Hiding or closing the window rests AUTO (INV-LC16): it keeps following the same MATCH when the
    // window shows again, and a reopened window of the same LISTEN continues it. LISTEN stays.
    juce::Component host;
    host.setBounds (-10'000, -10'000, 1, 1); host.addToDesktop (0);
    host.addAndMakeVisible (*r.editor); host.setVisible (true);
    require (r.editor->getPeer() != nullptr && r.editor->isShowing(), "native retained-editor host");
    (r.editor.get()->*privateMember (NoticeFollowChoice {})) (3);
    require (hiddenAutoOn (*r.editor, {}), "AUTO starts after the actual MATCH");
    host.setVisible (false); r.advance (0.03);
    require (hiddenAutoOn (*r.editor, {}) && ! hiddenAutoNotice (*r.editor, {})
                 && r.editor->isVisible() && ! r.editor->isShowing() && r.post->liveCompareStatus().active,
             "ancestor hide retains actual LISTEN and rests AUTO without a stop notice");
    host.setVisible (true); r.advance (0.03);
    require (hiddenAutoOn (*r.editor, {}) && r.view().feedback() != "AUTO stopped: editor hidden",
             "shown again, AUTO continues");
    host.removeChildComponent (r.editor.get());
    r.close(); r.advance (0.1); r.open(); r.advance (0.1);
    require (hiddenAutoOn (*r.editor, {}) && r.post->liveCompareStatus().active,
             "a reopened window continues the AUTO of the same LISTEN");
    verifyClosedAutoAcrossTimeline (r);
    (r.editor.get()->*privateMember (NoticeFollowChoice {})) (2);
    require (! hiddenAutoOn (*r.editor, {}), "Stop AUTO ends the continued AUTO");
    r.editor.get()->*privateMember (HiddenToastUntil {}) = 0.0; r.advance (0.03); // let "AUTO off" pass
    // 2026-10-13 decision: a short stop plays POST silently; only a wait beyond 3 seconds explains
    // the cause and how to return.
    r.clock.playing = false; r.advance (0.4);
    require (r.view().feedback().isEmpty(), "a short DAW stop shows no waiting notice");
    r.advance (2.8);
    require (r.view().feedback().isNotEmpty(), "a DAW stop beyond 3 seconds shows its remedy");
    r.clock.playing = true; r.advance (0.6);
    require (r.view().feedback().isEmpty(), "waiting remedy clears when verified PRE resumes");
    // An unexpected pair change during actual LISTEN remains a bounded terminal notice.
    r.post->clearPairCandidate(); r.advance (0.15);
    require (! r.post->liveCompareStatus().active && r.view().feedback().startsWith ("PRE changed:"),
             "actual comparison interruption is explained");
    r.expectOrdinaryAudio = true;
    r.advance (3.2); r.silentOrdinary();
    r.selectPair(); r.advance (0.15); r.silentOrdinary();
    require (r.post->startLiveCompare() == live_compare::StartResult::started, "new LISTEN after terminal notice");
    r.expectOrdinaryAudio = false; r.advance (0.5);
    verifyClosedAutoEnded (r);  // END while the window is closed
    r.expectOrdinaryAudio = true; r.block(); r.silentOrdinary();
    r.post->clearPairCandidate(); r.advance (0.15); r.silentOrdinary();
    host.removeChildComponent (r.editor.get()); host.setVisible (false);
    r.close(); r.open(); r.advance (0.15); r.silentOrdinary();
    require (r.pre->getLatencySamples() == 0 && r.post->getLatencySamples() == 0, "normal zero latency retained");
}
}
