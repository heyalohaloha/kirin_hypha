#pragma once

#include "EditorProductChecks.h"
#include "EditorHiddenAutoCheck.h"
#include "LiveTimingFixtureAccess.h"
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
        require (currentNamedPresentation (status, admission).action == RecoveryAction::stopPlay,
                 "a current timing hold survives END until actual stop/play");
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
    std::int64_t position = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing); info.setTimeInSamples (position);
        info.setKirinAuxiliaryClockSource (1); info.setKirinAuxiliaryClockSamples (position);
        info.setKirinPresentationLatencySource (1); info.setKirinOutputPresentationLatencySamples (0);
        return info;
    }
};

struct NoticeRig
{
    NoticeClock clock;
    std::unique_ptr<Processor> pre, post;
    std::unique_ptr<KirinHyphaEditor> editor;
    juce::AudioBuffer<float> preAudio { 2, 512 }, postAudio { 2, 512 };
    juce::MidiBuffer midi;
    bool expectOrdinaryAudio = true;
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
            p->setPlayHead (&clock); p->prepareToPlay (48000, 512);
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
            preAudio.setSample (c, n, value); postAudio.setSample (c, n, value);
        }
        pre->processBlock (preAudio, midi); post->processBlock (postAudio, midi);
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
        const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double> (seconds);
        while (std::chrono::steady_clock::now() < end)
        { block(); std::this_thread::sleep_for (std::chrono::milliseconds (11)); }
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
    // A retained editor whose parent is hidden differs from its own visibilityChanged(), which
    // ends LISTEN immediately. Exercise the native ancestor/peer path used by hidden hosts.
    juce::Component host;
    host.setBounds (-10'000, -10'000, 1, 1); host.addToDesktop (0);
    host.addAndMakeVisible (*r.editor); host.setVisible (true);
    require (r.editor->getPeer() != nullptr && r.editor->isShowing(), "native retained-editor host");
    const auto hiddenWithAction = [&]
    {
        enableHiddenAuto (*r.editor, {});
        host.setVisible (false); r.advance (0.03);
        require (! hiddenAutoOn (*r.editor, {}) && hiddenAutoNotice (*r.editor, {}), "hidden AUTO stops and queues once");
        require (r.editor->isVisible() && ! r.editor->isShowing() && r.post->liveCompareStatus().active,
                 "ancestor hide retains actual LISTEN and stops only AUTO follow");
        (r.editor.get()->*privateMember (ShowToast {})) ("Capture could not be attached");
        host.setVisible (true); r.advance (0.03);
        if (! hiddenAutoNotice (*r.editor, {}) || r.view().feedback() != "Capture could not be attached")
            std::cerr << "Hidden AUTO state: pending=" << hiddenAutoNotice (*r.editor, {})
                      << " on=" << hiddenAutoOn (*r.editor, {}) << " active=" << r.post->liveCompareStatus().active
                      << " reason=" << static_cast<int> (r.post->liveCompareStatus().reason)
                      << " feedback=" << r.view().feedback() << '\n';
        require (hiddenAutoNotice (*r.editor, {}) && r.view().feedback() == "Capture could not be attached",
                 "current hidden AUTO notice waits behind an explicit failure");
    };
    const auto expireAction = [&]
    { r.editor.get()->*privateMember (HiddenToastUntil {}) = 0.0; r.advance (0.03); };
    hiddenWithAction(); expireAction();
    require (! hiddenAutoOn (*r.editor, {}) && ! hiddenAutoNotice (*r.editor, {})
        && r.view().feedback() == "AUTO stopped: editor hidden", "current hidden AUTO result is delivered once");
    r.advance (3.1);
    hiddenWithAction();
    enableHiddenAuto (*r.editor, {}); r.advance (0.03); // eligible new AUTO command supersedes the queued result
    require (hiddenAutoOn (*r.editor, {}) && ! hiddenAutoNotice (*r.editor, {}), "resumed AUTO retires its previous stop notice");
    expireAction();
    require (r.view().feedback() != "AUTO stopped: editor hidden", "old AUTO stop cannot return after newer AUTO");
    r.clock.playing = false; r.advance (0.4);
    require (r.view().feedback().isNotEmpty(), "current DAW-stop remedy remains visible");
    r.clock.playing = true; r.advance (0.6);
    require (r.view().feedback().isEmpty(), "waiting remedy clears when verified PRE resumes");
    // An unexpected pair change during actual LISTEN remains a bounded terminal notice.
    hiddenWithAction();
    r.post->clearPairCandidate(); r.advance (0.15);
    require (! r.post->liveCompareStatus().active && r.view().feedback().startsWith ("PRE changed:"),
             "actual comparison interruption is explained");
    require (! hiddenAutoNotice (*r.editor, {}), "actual interruption supersedes the queued hidden AUTO result");
    r.expectOrdinaryAudio = true;
    r.advance (3.2); r.silentOrdinary();
    r.selectPair(); r.advance (0.15); r.silentOrdinary();
    require (r.post->startLiveCompare() == live_compare::StartResult::started, "new LISTEN after terminal notice");
    r.expectOrdinaryAudio = false; r.advance (0.5);
    r.post->finishLiveCompare(); r.advance (3.3);
    r.expectOrdinaryAudio = true; r.block(); r.silentOrdinary();
    r.post->clearPairCandidate(); r.advance (0.15); r.silentOrdinary();
    host.removeChildComponent (r.editor.get()); host.setVisible (false);
    r.close(); r.open(); r.advance (0.15); r.silentOrdinary();
    require (r.pre->getLatencySamples() == 0 && r.post->getLatencySamples() == 0, "normal zero latency retained");
}
}
