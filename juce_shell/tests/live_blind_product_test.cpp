#include "../src/PluginProcessor.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "ValidationStorageSandbox.h"
#include "LiveTimingFixtureAccess.h"
#include "LiveBlindLoopFixture.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
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

#include "LiveBlindEndContractTest.h"

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
            (sharedLoop != nullptr ? *sharedLoop : loop).decorate (info, position);
        }
        return info;
    }
    std::int64_t position = 0;
    bool playing = false;
    LiveBlindLoopFixture loop;
    const LiveBlindLoopFixture* sharedLoop = nullptr;
};

class BlindContract final : private juce::Timer
{
public:
    explicit BlindContract (std::vector<float> signalIn, bool reuseIn,
                            hypha::live_compare::RecoveryReason faultIn, bool approvalIn, bool loopIn = false)
        : signal (std::move (signalIn)), reuse (reuseIn), fault (faultIn != hypha::live_compare::RecoveryReason::none),
          approval (approvalIn), loopMode (loopIn), faultReason (faultIn)
    {
        postClock.sharedLoop = &clock.loop;
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            auto instance = std::make_unique<Processor> (role);
            auto layout = instance->getBusesLayout();
            layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
            layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
            require (instance->setBusesLayout (layout), "host negotiates stereo");
            instance->setMeterContextPreference (hypha::meter_context::MeterContext::twoMix, false);
            instance->setPlayHead (role == Processor::Role::Pre ? &clock : &postClock);
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

    // What a failure on a slow test machine needs to explain itself: where the round trip stood,
    // the session's state and reasons, and the longest stall of the fixture's own audio thread.
    void requireWithState (bool ok, const char* message) const
    {
        if (ok) return;
        const auto compare = post->liveCompareStatus();
        const auto blind = post->liveBlindStatus();
        std::cerr << "Live Blind product state: stage " << stage << ", blind stage " << static_cast<int> (blind.stage)
                  << ", blind reason " << static_cast<int> (blind.reason) << "/" << static_cast<int> (blind.observation)
                  << ", active " << compare.active << ", matched " << compare.matched << ", finishing " << compare.finishing
                  << ", gain " << compare.gain << ", reason " << static_cast<int> (compare.reason) << "/"
                  << static_cast<int> (compare.observation) << ", laps " << clock.loop.laps.load()
                  << ", longest callback interval " << longestCallbackMicros.load() / 1000.0 << " ms\n";
        require (false, message);
    }

    void timerCallback() override
    {
        requireWithState (std::chrono::steady_clock::now() - started
                              < std::chrono::seconds ((loopMode ? 145 : 60) + 45 * stallRestarts),
                          "Blind round trip timed out");
        // A stall of this machine past the callback-gap rule (2.5 blocks, 427 ms) rightly ends the
        // Blind preparation with a callback gap. Press BLIND again as a user would, at most twice;
        // a gap without such a stall is a product fault and fails here.
        if (stage == 3 && ! reuse && ! fault)
            if (const auto blind = post->liveBlindStatus(); blind.stage == hypha::live_compare::BlindStage::idle
                && blind.reason == hypha::live_compare::RecoveryReason::callbackGap)
            {
                requireWithState (machineStalls.load() > recoveredStalls, "a Blind callback gap follows a real test-machine stall");
                requireWithState (++stallRestarts <= 2, "Blind recovers from at most two test-machine stalls");
                recoveredStalls = machineStalls.load();
                std::cout << "Blind restarted after a test-machine stall of " << longestCallbackMicros.load() / 1000.0
                          << " ms" << std::endl;
                stage = 2;
                return;
            }
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
                    if (loopMode) { post->selectLiveComparePre (true); loopPcmAuditActive.store (true); clock.loop.requested.store (true); }
                    reused = true;
                    break;
                }
                if (reuse && post->liveBlindStatus().stage == hypha::live_compare::BlindStage::idle)
                {
                    if (! post->liveCompareStatus().matchReady) break;
                    if (loopMode)
                    {
                        if (clock.loop.laps.load() < 100) break;
                        require (loopPcmErrors.load() == 0 && loopPreFrames.load() >= 95 * LiveBlindLoopFixture::length,
                                 "100 named PRE laps output the correct delayed occurrence in every frame");
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
                if (loopMode && clock.loop.laps.load() < 200)
                {
                    requireWithState (post->liveBlindStatus().stage == hypha::live_compare::BlindStage::active
                        && post->liveCompareStatus().matched && std::abs (post->liveCompareStatus().gain - reusedGain) <= 0.0f,
                        "another 100 loop laps preserve the fixed MATCH and Blind trial");
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
                    if (faultReason == Reason::contentChanged) post->holdLiveCompareForContentJump (-3000);
                    else if (faultReason == Reason::callbackGap) injectGap.store (true);
                    else if (faultReason == Reason::stopped) play.store (false);
                    else post->kirinHostDelayCompensationStateChanged (false);
                    stage = 50;
                    break;
                }
                // The 20 ms fixture can precede the editor's 100 ms presentation tick.
                if (! stoppedEnd.ready (suspendAudio, suspended)) break;
                if (! revealReady) { revealReady = true; revealReadyAt = std::chrono::steady_clock::now(); }
                if (! click ("live-blind-reveal"))
                {
                    require (std::chrono::steady_clock::now() - revealReadyAt < std::chrono::seconds (2),
                             "one-click reveal becomes available after both source receipts");
                    break;
                }
                require (post->liveBlindStatus().trial.revealed, "assignment revealed by the button");
                require (post->liveBlindStatus().trial.firstPre == (firstRatio > 0.0f), "revealed mapping agrees with actual PCM");
                require (! post->revealLiveBlind(), "reveal cannot be repeated");
                require (post->requestLocalBlindProductCapture (hypha::meter_context::MeterContext::twoMix)
                         != hypha::local_blind::CaptureAdmission::ready, "Exact capture cannot overlap Blind");
                loopPcmAuditActive.store (false);
                require (click ("live-blind-end"), "END requests normal output");
                stoppedEnd.accepted (*post, *editor);
                require (post->liveCompareStatus().finishing, "END waits for actual audio receipt");
                ++stage;
                break;
            case 50:
                if (post->liveBlindStatus().stage != hypha::live_compare::BlindStage::invalidated) break;
                // The invalidation can be seen while the audio thread is still in the block that made
                // it, before the fixture stores that block's output: judge blocks made after it.
                if (invalidatedBlock < 0) invalidatedBlock = audioBlocks.load();
                if (audioBlocks.load() <= invalidatedBlock + 1) break;
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
                if (! stoppedEnd.resume (*post, *editor, suspendAudio)) break;
                post->serviceLiveCompare(); // consume the RT receipt before asserting non-RT cleanup
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
                require (! loopMode || (loopPcmErrors.load() == 0 && loopPreFrames.load() >= 95 * LiveBlindLoopFixture::length
                    && loopPostFrames.load() > 0), "full-frame delayed occurrence oracle passes for named PRE and both Blind sources");
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

   #include "LiveBlindAudioFixture.h"

    // A host block of 8192 frames (171 ms): the callback-gap rule then tolerates test-machine stalls
    // up to 427 ms. This product test checks the flow, not DAW scheduling or a low-buffer load
    // budget; those require the separate real-host matrix.
    static constexpr int blockFrames = 8192;
    Clock clock, postClock;
    std::vector<float> signal;
    std::unique_ptr<Processor> pre, post;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::thread audio;
    std::atomic<bool> running { true }, play { false };
    std::chrono::steady_clock::time_point started, listenedAt, requestedAt, revealReadyAt;
    hypha::pair_preview::Ticket preview;
    int stage = 0;
    bool reuse = false, fault = false, approval = false, reused = false, approved = false, revealReady = false;
    bool loopMode = false;
    int recoveredStalls = 0, stallRestarts = 0;
    float faultGain = 1.0f;
    hypha::live_compare::RecoveryReason faultReason;
    std::atomic<bool> injectGap { false };
    float reusedGain = 1.0f, held = 1.0f, firstRatio = 0.0f;
    int observedBlock = 0, invalidatedBlock = -1;
    std::atomic<float> lastOutputRatio { 0.0f };
    std::atomic<float> lastPcmError { 0.0f };
    std::atomic<int> audioBlocks { 0 };
    std::atomic<bool> offline { false };
    SuspendedBlindEndProbe stoppedEnd;
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
    // macOS JUCE resolves the home without HOME; keep PRE display files out of the real Kirin OS.
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
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
