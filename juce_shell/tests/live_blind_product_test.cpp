#include "../src/PluginProcessor.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "ValidationStorageSandbox.h"
#include "LiveBlindLoopFixture.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>

static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "Live Blind product: " << message << '\n'; std::exit (EXIT_FAILURE); }
}
#include "LocalBlindSignalFixture.h"
#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

// One-pass Blind end to end (disposable fixture): real PRE/POST processors, Rust C ABI, pair
// discovery, shared live ring and product editor. Default POST is phase-inverted at -6.02 dB;
// the approval mode clips a boosted input. Direct and reused MATCH entries use one playback,
// without PIN/capture, and the revealed mapping must agree with actual output PCM.
namespace
{
using Processor = KirinHyphaProcessorBase;
using Phase = hypha::local_blind::ProductSessionPhase;

juce::Component* find (juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
        if (auto* child = find (*parent.getChildComponent (i), id)) return child;
    return nullptr;
}

const hypha::observatory::View* findView (juce::Component& parent)
{
    if (auto* view = dynamic_cast<const hypha::observatory::View*> (&parent)) return view;
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
        if (auto* view = findView (*parent.getChildComponent (i))) return view;
    return nullptr;
}

struct Clock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        if (playing)
        {
            info.setTimeInSamples (position);
            info.setTimeInSeconds (static_cast<double> (position) / 48000.0);
            loop.decorate (info, position);
        }
        return info;
    }
    std::int64_t position = 0;
    bool playing = false;
    LiveBlindLoopFixture loop;
};

class BlindContract final : private juce::Timer
{
public:
    explicit BlindContract (std::vector<float> signalIn, bool reuseIn,
                            hypha::live_compare::RecoveryReason faultIn, bool approvalIn, bool loopIn = false)
        : signal (std::move (signalIn)), reuse (reuseIn), fault (faultIn != hypha::live_compare::RecoveryReason::none),
          approval (approvalIn), loopMode (loopIn), faultReason (faultIn)
    {
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            auto instance = std::make_unique<Processor> (role);
            auto layout = instance->getBusesLayout();
            layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
            layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
            require (instance->setBusesLayout (layout), "host negotiates stereo");
            instance->setMeterContextPreference (hypha::meter_context::MeterContext::twoMix, false);
            instance->setPlayHead (&clock);
            instance->setNonRealtime (false);
            instance->prepareToPlay (48000, blockFrames);
            (role == Processor::Role::Pre ? pre : post) = std::move (instance);
        }
        editor.reset (post->createEditorIfNeeded());
        require (editor != nullptr, "real product editor opens");
        editor->setSize (900, 600);
        editor->setVisible (true);
        started = std::chrono::steady_clock::now();
        audio = std::thread ([this] { processAudio(); });
        startTimer (20);
    }

    ~BlindContract() override
    {
        stopTimer();
        running.store (false);
        if (audio.joinable()) audio.join();
        if (editor != nullptr) post->editorBeingDeleted (editor.get());
        editor.reset();
        pre->releaseResources();
        post->releaseResources();
    }

    bool passed = false;

private:
    juce::String footer() const
    {
        const auto* view = findView (*editor);
        return view != nullptr ? view->feedback() : juce::String();
    }

    bool click (const char* id)
    {
        auto* button = dynamic_cast<juce::Button*> (find (*editor, id));
        if (button == nullptr || ! button->onClick || ! button->isEnabled() || ! button->isVisible())
            return false;
        button->onClick();
        return true;
    }

    void timerCallback() override
    {
        require (std::chrono::steady_clock::now() - started < std::chrono::seconds (loopMode ? 110 : 60), "Blind round trip timed out");
        switch (stage)
        {
            case 0:
            {
                if (pre->instanceId().isEmpty()) break;
                if (! preview)
                    preview = post->createPairPreview();
                if (! demanded)
                {
                    if (! hypha::pair_preview::request (preview)) break;
                    demanded = true;
                    requestedAt = std::chrono::steady_clock::now();
                }
                KirinPairPreviewValue value {};
                if (! kirin_hypha_pair_preview_poll (preview.get(), &value)) break;
                if (! value.complete || ! value.has_single)
                {
                    if (std::chrono::steady_clock::now() - requestedAt >= std::chrono::milliseconds (1050)
                        && hypha::pair_preview::request (preview))
                        requestedAt = std::chrono::steady_clock::now();
                    break;
                }
                require (post->setPairCandidate (pre->instanceId(), {}), "the discovered PRE is selected");
                preview.reset();
                ++stage;
                break;
            }
            case 1:
                if (post->pairStatus() != KIRIN_PAIR_STATUS_PAIRED) break;
                play.store (true);
                ++stage;
                break;
            case 2:
                if (! post->isPlaying() || ! post->heartbeatLive()) break;
                if (reuse)
                {
                    if (! click ("observatory-live-compare")) break;
                }
                else if (! click ("observatory-local-blind")) break;
                listenedAt = std::chrono::steady_clock::now();
                ++stage;
                break;
            case 3:
                if (approval && post->liveBlindStatus().stage == hypha::live_compare::BlindStage::approval)
                {
                    require (post->liveCompareStatus().postTarget >= 1.0f, "automatic MATCH does not lower POST without approval");
                    require (! post->approveLiveBlindMatch (post->liveBlindStatus().generation - 1), "stale approval is refused");
                    if (! click ("live-blind-approve")) break;
                    approved = true;
                }
                if (reuse && ! reused)
                {
                    if (std::chrono::steady_clock::now() - listenedAt < std::chrono::milliseconds (4500)) break;
                    const auto measured = post->measureLiveCompare();
                    if (! measured.ok()) break;
                    require (std::abs (measured.measuredDb + 6.0206) < 0.002, "MATCH measures -6.02 dB");
                    const auto plan = hypha::live_compare::planMatch (measured, 0.0);
                    auto stale = plan;
                    --stale.generation;
                    require (stale.generationBound && ! post->applyLiveCompareMatch (stale, hypha::live_compare::MatchChoice::basis),
                             "old measurement generations cannot apply a MATCH");
                    auto staleProof = plan;
                    ++staleProof.proof;
                    require (staleProof.proofBound && post->applyLiveCompareMatch (staleProof,
                        hypha::live_compare::MatchChoice::basis).failure == hypha::live_compare::MatchFailure::stale,
                        "an expired measurement proof cannot apply its gain");
                    require (post->applyLiveCompareMatch (plan, hypha::live_compare::MatchChoice::basis),
                             "manual MATCH accepted");
                    reusedGain = post->liveCompareStatus().gain;
                    if (loopMode) clock.loop.requested.store (true);
                    reused = true;
                    break;
                }
                if (reuse && post->liveBlindStatus().stage == hypha::live_compare::BlindStage::idle)
                {
                    if (! post->liveCompareStatus().matchReady) break;
                    if (loopMode)
                    {
                        if (clock.loop.laps.load() < 8) break;
                        const auto loopMatch = post->measureLiveCompare();
                        require (loopMatch.ok() && loopMatch.seconds >= 3.0
                            && std::abs (loopMatch.measuredDb + 6.0206) < 0.002,
                            "short loops accumulate a real, continuous MATCH window");
                    }
                    if (! click ("observatory-local-blind")) break; // editor observes the RT receipt on its own tick
                }
                if (post->liveBlindStatus().stage != hypha::live_compare::BlindStage::active) break;
                require (! approval || approved, "limited-master fixture required explicit approval");
                require (! post->liveCompareStatus().finishing, "trial is not returning");
                if (reuse) require (std::abs (post->liveCompareStatus().gain - reusedGain) <= 0.0f, "existing MATCH is reused exactly");
                require (! post->revealLiveBlind(), "both source receipts are required before reveal");
                require (find (*editor, "live-blind-screen")->isVisible(), "anonymous screen opens");
                require (! findView (*editor)->isAccessible() && ! findView (*editor)->isEnabled(),
                         "underlying identity-bearing UI and accessibility are isolated");
                require (! find (*editor, "observatory-live-pre")->isAccessible(), "descendant identity is inaccessible too");
                require (post->getLatencySamples() == 0 && pre->getLatencySamples() == 0, "zero latency");
                observedBlock = audioBlocks.load();
                ++stage;
                break;
            case 4:
                if (loopMode && clock.loop.laps.load() < 100)
                {
                    require (post->liveBlindStatus().stage == hypha::live_compare::BlindStage::active
                        && post->liveCompareStatus().matched && std::abs (post->liveCompareStatus().gain - reusedGain) <= 0.0f,
                        "100 loop laps preserve the fixed MATCH and Blind trial");
                    break;
                }
                if (post->liveBlindStatus().trial.played != 1 || audioBlocks.load() <= observedBlock + 1) break;
                firstRatio = lastOutputRatio.load();
                require (approval ? lastPcmError.load() < 0.0001f : std::abs (std::abs (firstRatio) - 0.5f) < 0.0001f,
                         "Source 1 actually outputs matched PCM");
                require (click ("live-blind-source-2"), "source 2 can be chosen in the same playback");
                observedBlock = audioBlocks.load();
                ++stage;
                break;
            case 5:
                if (post->liveBlindStatus().trial.played != 3 || audioBlocks.load() <= observedBlock + 1) break;
                require (approval ? lastOutputRatio.load() * firstRatio < 0.0f && lastPcmError.load() < 0.0001f
                                  : std::abs (lastOutputRatio.load() + firstRatio) < 0.0001f,
                         "Source 2 actually outputs the other PCM");
                if (fault)
                {
                    using Reason = hypha::live_compare::RecoveryReason;
                    faultGain = post->liveCompareStatus().gain;
                    if (faultReason == Reason::contentChanged) post->holdLiveCompareForContentJump();
                    else if (faultReason == Reason::callbackGap) injectGap.store (true);
                    else if (faultReason == Reason::stopped) play.store (false);
                    else post->kirinHostDelayCompensationStateChanged (false);
                    stage = 50;
                    break;
                }
                require (click ("live-blind-reveal"), "one click reveals without a menu or an answer");
                require (post->liveBlindStatus().trial.revealed, "assignment revealed by the button");
                require (post->liveBlindStatus().trial.firstPre == (firstRatio > 0.0f), "revealed mapping agrees with actual PCM");
                require (! post->revealLiveBlind(), "reveal cannot be repeated");
                require (post->requestLocalBlindProductCapture (hypha::meter_context::MeterContext::twoMix)
                         != hypha::local_blind::CaptureAdmission::ready, "Exact capture cannot overlap Blind");
                require (click ("live-blind-end"), "END requests normal output");
                require (post->liveCompareStatus().finishing, "END waits for actual audio receipt");
                ++stage;
                break;
            case 50:
                if (post->liveBlindStatus().stage != hypha::live_compare::BlindStage::invalidated) break;
                require (! post->revealLiveBlind() && ! post->selectLiveBlind (1), "PDC loss invalidates instead of restarting");
                require (post->liveBlindStatus().reason == faultReason,
                         "the first fault survives RT invalidation and message-thread teardown");
                if (faultReason != hypha::live_compare::RecoveryReason::contentChanged)
                    require (! post->liveCompareStatus().matched
                        && std::abs (post->liveCompareStatus().gain - faultGain) <= 0.0f,
                        "an ended Blind retains numeric gain but never a valid MATCH");
                require (lastOutputRatio.load() < 0.0f, "PDC failure falls back to original POST");
                post->kirinHostDelayCompensationStateChanged (true);
                play.store (true);
                post->serviceLiveBlind();
                require (post->liveBlindStatus().reason == faultReason,
                         "later recovery does not rewrite the stopped trial's reason");
                if (faultReason == hypha::live_compare::RecoveryReason::callbackGap)
                {
                    editor->giveAwayKeyboardFocus();
                    post->editorBeingDeleted (editor.get()); editor.reset();
                    editor.reset (post->createEditorIfNeeded());
                    editor->setSize (900, 600); editor->setVisible (true);
                    require (post->liveCompareStatus().reason == faultReason,
                             "close and reopen retain the specific Blind failure, not teardown's unknown");
                    stage = 51; // admission and footer arrive on the new editor's first tick
                    break;
                }
                else require (click ("live-blind-end"), "invalidated trial retains END");
                stage = 6;
                break;
            case 51:
                if (! click ("observatory-live-compare")) break;
                require (post->liveCompareStatus().reason == hypha::live_compare::RecoveryReason::none,
                         "a new explicit LISTEN clears the old trial cause");
                require (click ("observatory-live-end"), "new named session has a real END");
                stage = 6;
                break;
            case 6:
                if (post->liveCompareStatus().active || post->liveCompareStatus().finishing) break;
                if (find (*editor, "live-blind-screen")->isVisible()) break;
                require (post->liveCompareStatus().postActual == 1.0f, "END completed at actual unity");
                require (post->liveBlindStatus().reason == hypha::live_compare::RecoveryReason::none,
                         "explicit END clears the retained cause only after its output receipt");
                require (findView (*editor)->isAccessible() && findView (*editor)->isEnabled(),
                         "measurement accessibility restored only after END");
                require (post->startLiveCompare() == hypha::live_compare::StartResult::started,
                         "scope released; another session starts");
                {
                    hypha::live_compare::MatchResult measured;
                    measured.failure = hypha::live_compare::MatchFailure::none;
                    measured.measuredDb = 8.0; measured.prePeakDbtp = -1.0;
                    measured.postPeakDbtp = -4.0; measured.ceilingDbtp = -1.0;
                    const auto plan = hypha::live_compare::planMatch (measured, 0.0);
                    require (plan.needsApproval && post->applyLiveCompareMatch (
                        plan, hypha::live_compare::MatchChoice::lowerPost), "explicit lower POST approval");
                }
                ++stage;
                break;
            case 7:
                if (std::abs (post->liveCompareStatus().postActual - std::pow (10.0f, -8.0f / 20.0f)) > 0.0001f) break;
                held = post->liveCompareStatus().postActual;
                post->closeLiveBlind();
                listenedAt = std::chrono::steady_clock::now();
                ++stage;
                break;
            case 8:
                if (std::chrono::steady_clock::now() - listenedAt < std::chrono::milliseconds (450)) break;
                require (std::abs (post->liveCompareStatus().postActual - held) <= 0.0f, "window close holds approved attenuation");
                suspendAudio.store (true);
                ++stage;
                break;
            case 9:
                if (! suspended.load()) break;
                post->finishLiveCompare();
                post->finishLiveCompare(); // idempotent while pending
                listenedAt = std::chrono::steady_clock::now();
                ++stage;
                break;
            case 10:
                if (std::chrono::steady_clock::now() - listenedAt < std::chrono::milliseconds (250)) break;
                require (post->liveCompareStatus().finishing && std::abs (post->liveCompareStatus().postActual - held) <= 0.0f,
                         "without callbacks END stays pending and never claims unity");
                offline.store (true);
                suspendAudio.store (false);
                observedBlock = audioBlocks.load();
                stage = 12;
                break;
            case 12:
                if (audioBlocks.load() <= observedBlock + 2) break;
                require (post->liveCompareStatus().finishing && std::abs (post->liveCompareStatus().postActual - held) <= 0.0f,
                         "offline callbacks cannot release attenuation or complete END");
                offline.store (false);
                stage = 11;
                break;
            case 11:
                if (post->liveCompareStatus().finishing) break;
                require (post->liveCompareStatus().postActual == 1.0f, "held POST returns to exact unity");
                require (rawPostErrors.load() == 0, "ordinary A output is bit identical");
                std::cout << "Live Blind product: PASS entry=" << (reuse ? "MATCH reuse" : "direct")
                          << " one continuous playback, both receipts, reveal, END, hold and resume\n";
                passed = true;
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
            default:
                break;
        }
    }

    float processedInput (float input) const
    {
        return approval ? -std::max (-0.8f, std::min (0.8f, input * 8.0f)) : input * -0.5f;
    }

    void processAudio()
    {
        juce::AudioBuffer<float> buffer (2, blockFrames);
        juce::MidiBuffer midi;
        auto next = std::chrono::steady_clock::now();
        while (running.load())
        {
            if (suspendAudio.load())
            {
                suspended.store (true);
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
                next = std::chrono::steady_clock::now();
                continue;
            }
            suspended.store (false);
            if (injectGap.exchange (false))
            {
                std::this_thread::sleep_for (std::chrono::milliseconds (600));
                next = std::chrono::steady_clock::now();
            }
            clock.playing = play.load();
            clock.loop.advance (clock.position);
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < blockFrames; ++f)
                    buffer.setSample (c, f, signal[static_cast<std::size_t> (clock.position + f) % signal.size()]);
            pre->processBlock (buffer, midi);
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < blockFrames; ++f)
                    buffer.setSample (c, f, processedInput (buffer.getSample (c, f)));
            const auto before = post->liveCompareStatus();
            const bool renderedOffline = offline.load();
            post->setNonRealtime (renderedOffline);
            post->processBlock (buffer, midi);
            lastOutputRatio.store (buffer.getSample (0, blockFrames - 1)
                / signal[static_cast<std::size_t> (clock.position + blockFrames - 1) % signal.size()]);
            const float inputEnd = signal[static_cast<std::size_t> (clock.position + blockFrames - 1) % signal.size()];
            const float outputEnd = buffer.getSample (0, blockFrames - 1);
            lastPcmError.store (std::min (std::abs (outputEnd - inputEnd * before.gain),
                                        std::abs (outputEnd - processedInput (inputEnd) * before.postActual)));
            audioBlocks.fetch_add (1);
            if (renderedOffline || (! before.active && ! before.finishing && before.postActual >= 1.0f
                && ! post->liveCompareStatus().active && post->liveCompareStatus().postTarget >= 1.0f))
                for (int c = 0; c < 2; ++c)
                    for (int f = 0; f < blockFrames; ++f)
                    {
                        const float expected = processedInput (signal[static_cast<std::size_t> (clock.position + f) % signal.size()]);
                        const float actual = buffer.getSample (c, f);
                        if (std::memcmp (&actual, &expected, sizeof (float)) != 0)
                            rawPostErrors.fetch_add (1);
                    }
            if (clock.playing) clock.position += blockFrames;
            next += std::chrono::nanoseconds (static_cast<long long> (blockFrames) * 1'000'000'000 / 48000);
            std::this_thread::sleep_until (next);
        }
    }

    // A host block of 8192 frames (171 ms): the callback-gap rule then tolerates test-machine stalls
    // up to 427 ms. This product test checks the flow, not DAW scheduling or a low-buffer load
    // budget; those require the separate real-host matrix.
    static constexpr int blockFrames = 8192;
    Clock clock;
    std::vector<float> signal;
    std::unique_ptr<Processor> pre, post;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::thread audio;
    std::atomic<bool> running { true }, play { false };
    std::chrono::steady_clock::time_point started, listenedAt, requestedAt;
    hypha::pair_preview::Ticket preview;
    int stage = 0;
    bool reuse = false, fault = false, approval = false, reused = false, approved = false;
    bool loopMode = false;
    float faultGain = 1.0f;
    hypha::live_compare::RecoveryReason faultReason;
    std::atomic<bool> injectGap { false };
    float reusedGain = 1.0f, held = 1.0f, firstRatio = 0.0f;
    int observedBlock = 0;
    std::atomic<float> lastOutputRatio { 0.0f };
    std::atomic<float> lastPcmError { 0.0f };
    std::atomic<int> audioBlocks { 0 };
    std::atomic<bool> offline { false };
    std::atomic<bool> suspendAudio { false }, suspended { false };
    std::atomic<int> rawPostErrors { 0 };
    bool demanded = false;
};
}

int main (int argc, char** argv)
{
    require (argc >= 2, "usage: live Blind product test S-1.wav [--reuse]");
    auto signal = readFixture (argv[1]);
    ValidationStorageSandbox sandbox;
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    hypha::i18n::holdLanguage (true);
    using Reason = hypha::live_compare::RecoveryReason;
    const auto mode = argc == 3 ? std::string (argv[2]) : std::string();
    const auto fault = mode == "--fault" ? Reason::compensationOff
        : mode == "--fault-gap" ? Reason::callbackGap : mode == "--fault-content" ? Reason::contentChanged
        : mode == "--fault-stop" ? Reason::stopped : Reason::none;
    BlindContract contract (std::move (signal), mode == "--reuse" || mode == "--loop", fault, mode == "--approval", mode == "--loop");
    juce::MessageManager::getInstance()->runDispatchLoop();
    return contract.passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
