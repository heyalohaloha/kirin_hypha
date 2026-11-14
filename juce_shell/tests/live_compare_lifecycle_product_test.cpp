#include "FiniteComparison.h"
#include "ProductTestClock.h"
#include "../src/PluginProcessor.h"
#include "ReferenceValidationStorageSandbox.h"
#include "LiveTimingFixtureAccess.h"
#include "LiveCompareEndMatchTest.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include "TestProcessorStorage.h"
static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "Live lifecycle: " << message << '\n'; std::exit (EXIT_FAILURE); }
}
#include "LocalBlindSignalFixture.h"
#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

namespace
{
using Processor = KirinHyphaProcessorBase;
using namespace hypha::live_compare;
using Steady = std::chrono::steady_clock;
struct Clock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setTimeInSamples (position);
        info.setTimeInSeconds (static_cast<double> (position) / 48000.0);
        return info;
    }
    std::int64_t position = 0;
    bool playing = false;
};

// Real processors/FFI/ring with disposable storage, no DAW or hardware. Block message service
// during restore and the first following audio callback to prove independent RT revocation.
class Lifecycle final : private juce::Timer
{
public:
    Lifecycle (std::vector<float> input, std::string scenario)
        : signal (std::move (input)), mode (std::move (scenario))
    {
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            auto instance = std::make_unique<Processor> (role);
            auto layout = instance->getBusesLayout();
            layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
            layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
            require (instance->setBusesLayout (layout), "stereo layout");
            instance->setMeterContextPreference (hypha::meter_context::MeterContext::twoMix, false);
            instance->setPlayHead (&clock);
            hypha::tests::prepareFixtureProcessor (*instance, 48000, frames);
            (role == Processor::Role::Pre ? pre : post) = std::move (instance);
        }
        audio = std::thread ([this] { processAudio(); });
        startTimer (20);
    }
    ~Lifecycle() override { stopTimer(); running.store (false); audio.join(); if (editor) post->editorBeingDeleted (editor.get()); }
    bool passed = false;
    bool staleChecked = false, toggled = false, lastSideChecked = false, capChecked = false, preToggled = false;
    float matchedPost = 1.0f;
    int stoppedAt = 0;

private:
    template <typename Predicate> void waitWithoutMessageService (Predicate&& ready, int timeoutMs = 350)
    {
        const auto deadline = Steady::now() + std::chrono::milliseconds (timeoutMs);
        while (! ready())
        {
            require (Steady::now() < deadline, "audio handoff deadline");
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }
    #include "LiveCompareLifecycleHelpers.h"
    void lowerPost (double db)
    {
        MatchResult measured;
        measured.failure = MatchFailure::none;
        measured.measuredDb = db;
        measured.prePeakDbtp = -1.0;
        measured.postPeakDbtp = -4.0;
        measured.ceilingDbtp = -1.0;
        require (post->applyLiveCompareMatch (planMatch (measured, 0.0), MatchChoice::lowerPost), "explicit attenuation accepted");
        held = static_cast<float> (std::pow (10.0, -db / 20.0));
        MatchPlan oldBasis;
        require (post->applyLiveCompareMatch (oldBasis, MatchChoice::basis).failure == MatchFailure::stale,
                 "an older unity-basis plan cannot raise the newly approved POST level");
    }
    void endWithoutMessageService()
    {
        post->finishLiveCompare();
        require (! post->liveCompareStatus().active, "END immediately closes the logical session");
        // Do not pump messages: audio receipt must not revive the old session before retirement.
        // PRE fades out first, then POST rises over up to 0.5 s, in host-sized callbacks.
        waitWithoutMessageService ([this] { return ! post->liveCompareStatus().finishing; }, 3500);
        require (! post->liveCompareStatus().active, "audio receipt cannot reopen the closed session");
        post->selectLiveComparePre (true);
        require (! post->liveCompareStatus().preSelected, "stale PRE selection rejected before message cleanup");
        require (post->liveCompareNeedsService(), "completed audio still schedules ring retirement");
        expectedPost.store (1.0f);
        verifyPost.store (true);
        const auto before = blocks.load();
        waitWithoutMessageService ([&] { return blocks.load() > before + 1; }, 700);
        require (postErrors.load() == 0, "closed session stays unity POST before message cleanup");
        verifyPost.store (false);
    }
    void timerCallback() override
    {
        require (Steady::now() - began < std::chrono::seconds (45), "scenario timed out");
        switch (stage)
        {
            case 0:
            {
                if (! hypha::tests::fixtureStorageReady (*pre) || ! hypha::tests::fixtureStorageReady (*post)) break;
                if (! preview) preview = post->createPairPreview();
                if (! demanded)
                {
                    if (! hypha::pair_preview::request (preview)) break;
                    demanded = true;
                    requestedAt = Steady::now();
                }
                KirinPairPreviewValue value {};
                if (! kirin_hypha_pair_preview_poll (preview.get(), &value)) break;
                if (! value.complete || ! value.has_single)
                {
                    if (Steady::now() - requestedAt > std::chrono::milliseconds (1050)
                        && hypha::pair_preview::request (preview)) requestedAt = Steady::now();
                    break;
                }
                require (post->setPairCandidate (pre->instanceId(), {}), "discovered pair selected");
                preview.reset();
                stage = 1;
                break;
            }
            case 1:
                if (post->pairStatus() != KIRIN_PAIR_STATUS_PAIRED) break;
                playing.store (true);
                stage = 2;
                break;
            case 2:
                if (! post->isPlaying() || ! post->heartbeatLive()) break;
                if (mode == "close-fade")
                {
                    require (post->startLiveCompare() == StartResult::started, "named fade fixture starts");
                    post->selectLiveComparePre (true); stage = 9; break;
                }
                if (mode == "held" && ! staleChecked)
                {   // A waiting LISTEN keeps the ticket of its press: a restore of the same state after the
                    // editor checked it refuses the start under the pair lock; a new press then starts.
                    const auto pressed = post->restoreOrPairTicket();
                    juce::MemoryBlock same;
                    post->getStateInformation (same);
                    post->setStateInformation (same.getData(), static_cast<int> (same.getSize()));
                    require (post->startLiveCompare (pressed) != StartResult::started && ! post->liveCompareStatus().active,
                             "a LISTEN press from before a restore of the same state does not start");
                    staleChecked = true;
                    break;
                }
                if (mode == "held" || mode == "finishing" || mode == "out-of-range"
                    || mode == "reuse-held" || mode == "restore-matched-listen")
                {
                    require (post->startLiveCompare() == StartResult::started, "named session starts");
                    // LISTEN が POST を使っているあいだ、Reference は A を下げない（POST を二重に下げない）。B・C・V は取って代わる。
                    const auto lowering = post->outputDecision (hypha::output_owner::Activity::lowerA);
                    require (lowering.refused() && lowering.reason == hypha::output_owner::Reason::liveComparison
                                 && ! post->outputDecision (hypha::output_owner::Activity::audition).refused(),
                             "LISTEN refuses a Reference A lowering and yields to an audition");
                    lowerPost (mode == "reuse-held" || mode == "restore-matched-listen" ? 8.0 : 20.0);
                    stage = 3;
                }
                else
                {
                    require (post->beginLiveBlind() == StartResult::started, "direct Blind starts");
                    if (mode == "restore-preparing") { restore(); stage = 6; }
                    else stage = 4;
                }
                break;
            case 3:
                if (std::abs (post->liveCompareStatus().postActual - held) > 0.0f) break;
                if (mode == "held")
                {
                    post->closeLiveBlind();
                    require (post->liveCompareAdmission (false) == StartResult::returnRequired
                        && post->liveCompareAdmission (true) == StartResult::returnRequired, "all new entries require RETURN");
                    require (post->startLiveCompare() == StartResult::returnRequired
                        && post->beginLiveBlind() == StartResult::returnRequired, "held gate cannot be bypassed through API");
                    // 保持中は、ほかの試聴も Reference の下げも始めない（INV-LC14）。
                    for (auto activity : { hypha::output_owner::Activity::audition, hypha::output_owner::Activity::lowerA,
                                           hypha::output_owner::Activity::versionBlind, hypha::output_owner::Activity::localBlind })
                        require (post->outputDecision (activity).refused(), "a held POST attenuation keeps every other comparison waiting");
                    restore(); stage = 6;
                }
                else if (mode == "finishing") { restore (true); stage = 8; }
                else if (mode == "out-of-range")
                {
                    post->setLiveCompareGain (1.0f); // only MATCH lost, session approval still valid
                    require (post->beginLiveBlind() == StartResult::started, "same-session preparation remains allowed");
                    stage = 4;
                }
                else
                {
                    const auto measured = post->measureLiveCompare();
                    if (! measured.ok()) break;
                    if (mode == "restore-matched-listen")
                    {
                        editor.reset (dynamic_cast<KirinHyphaEditor*> (post->createEditorIfNeeded()));
                        require (editor != nullptr, "shipping editor for AUTO");
                        end_match_test::apply (*editor, planMatch (measured, -8.0));
                        require (post->liveCompareStatus().matched, "editor applies the same-session MATCH");
                    }
                    else require (post->applyLiveCompareMatch (planMatch (measured, -8.0), MatchChoice::basis), "full same-session MATCH");
                    stage = 5;
                }
                break;
            case 5:
                if (! post->liveCompareStatus().matchReady) break;
                if (mode == "restore-matched-listen" && ! toggled)
                {   // MATCH on/off (INV-LC13): off returns PRE and POST to their levels when LISTEN began.
                    const auto on = post->liveCompareStatus();
                    matchedGain = on.gain; matchedPost = on.postTarget;
                    end_match_test::enableAuto (*editor);
                    require (end_match_test::autoOn (*editor, {}) && matchedPost < 1.0f, "AUTO enabled before MATCH off");
                    end_match_test::toggle (*editor);
                    const auto off = post->liveCompareStatus();
                    require (off.matchOff && off.gain == 1.0f && off.postTarget == 1.0f && ! off.matched && ! off.matchHeld
                        && ! off.matchReady, "MATCH off: PRE at its own level, POST back, nothing for BLIND to reuse");
                    require (! post->setLiveCompareMatchOn (false).turned, "a second off changes nothing");
                    toggled = true; stage = 10;
                    break;
                }
                if (mode == "restore-matched-listen")
                {
                    post->selectLiveComparePre (true);
                    stage = 7;
                    break;
                }
                matchedGain = post->liveCompareStatus().gain;
                require (post->beginLiveBlind() == StartResult::started, "MATCH to Blind carries same-session approval");
                stage = 4;
                break;
            case 7:
                if (! post->liveCompareStatus().preAudible) break;
                if (mode == "restore-matched-listen" && ! preToggled)
                {   // A gain write in progress keeps PRE: the block uses the last whole tuple, never POST.
                    pause.store (true); waitWithoutMessageService ([this] { return paused.load(); });
                    const auto playsPre = [this] (const char* why)
                    {
                        const auto before = blocks.load();
                        pause.store (false); waitWithoutMessageService ([&] { return blocks.load() > before + 3; }, 1000);
                        require (ratio.load() > 0.0f && post->liveCompareStatus().preAudible, why);
                    };
                    { GainUpdate hold (LiveTimingFixtureAccess::gainRevision (*post)); require (static_cast<bool> (hold), "lease held");
                      playsPre ("PRE keeps playing while a gain write holds the revision"); }
                    require (post->setLiveCompareMatchOn (false).turned && post->setLiveCompareMatchOn (true).turned, "MATCH off and on");
                    playsPre ("PRE stays audible through MATCH off and on, with its ramps");
                    preToggled = true; stage = 12;
                    break;
                }
                restore(); stage = 6;
                break;
            case 12:
                if (post->liveCompareStatus().postActual > matchedPost || ! post->liveCompareStatus().matchReady) break;
                restore(); stage = 6;
                break;
            case 4:
            {
                post->serviceLiveBlind();
                const auto status = post->liveBlindStatus();
                if (mode == "restore-approval")
                {
                    // A limited master: BLIND lowers POST at once (no approval stage) and a restore
                    // then holds that attenuation like any other.
                    require (status.stage != BlindStage::approval, "BLIND never waits for a POST-lowering approval");
                    if (post->liveCompareStatus().postActual >= 1.0f) break;
                    restore(); stage = 6; break;
                }
                if (mode == "out-of-range")
                {
                    if (status.stage != BlindStage::failed) break;
                    require (status.waiting == MatchFailure::outOfRange && ! post->liveCompareStatus().active,
                             "final -26.021 dB fails explicitly, never endlessly prepares");
                    require (std::abs (post->liveCompareStatus().postActual - held) <= 0.0f, "failed plan preserves attenuation");
                    checkedAt = Steady::now(); stage = 6; break;
                }
                if (status.stage != BlindStage::active) break;
                if (mode == "reuse-held")
                {
                    require (std::abs (post->liveCompareStatus().gain - matchedGain) <= 0.0f
                        && std::abs (post->liveCompareStatus().postActual - held) <= 0.0f,
                             "same-session gains reused unchanged");
                    endWithoutMessageService(); stage = 8; break;
                }
                if (status.trial.played == 1) require (post->selectLiveBlind (2), "second source selected");
                if (status.trial.played != 3) break;
                // Choose audible PRE by fixture PCM sign, never by information exposed to users.
                if (ratio.load() < 0.0f) { require (post->selectLiveBlind (1), "select PRE before restore"); break; }
                restore(); stage = 6;
                break;
            }
            case 6:
                if (Steady::now() - checkedAt < std::chrono::milliseconds (500)) break;
                require (! post->liveCompareStatus().active && ! post->revealLiveBlind(), "old trial never resumes after service");
                require (std::abs (post->liveCompareStatus().postActual - held) <= 0.0f
                    && postErrors.load() == 0, "held output stays unchanged");
                verifyPost.store (false);
                post->finishLiveCompare(); stage = 8;
                break;
            case 8:
                post->serviceLiveCompare();
                if (post->liveCompareStatus().finishing) break;
                require (post->liveCompareStatus().postActual == 1.0f && ! post->liveCompareStatus().active, "END confirmed at unity");
                if (mode == "restore-matched-listen" && ! lastSideChecked)
                {   // PRE was chosen before the restore: the project keeps it, and only a LISTEN press uses it.
                    require (post->startLiveCompare (std::nullopt, true) == StartResult::started
                        && post->liveCompareStatus().preSelected, "the next LISTEN starts on PRE, the side last chosen");
                    post->finishLiveCompare(); lastSideChecked = true;
                    break;
                }
                require (post->beginLiveBlind() == StartResult::started, "new explicit session starts after RETURN");
                require (! post->liveCompareStatus().preSelected, "BLIND does not start on LISTEN's side");
                post->closeLiveBlind();
                std::cout << "Live lifecycle: PASS " << mode << " postErrors=" << postErrors.load() << '\n';
                passed = true;
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
            case 9:
                if (post->liveCompareStatus().preAudible && ratio.load() > 0.99f) closeWithFade();
                break;
            case 10: // POST rises back within half a second; then playback stops and starts again while off
                if (post->liveCompareStatus().postActual >= 1.0f) { playing.store (false); stoppedAt = blocks.load(); stage = 13; }
                break;
            case 13:
                if (blocks.load() >= stoppedAt + 20) { playing.store (true); stage = 14; }
                break;
            case 14: // timing is back: MATCH on restores the same match for this playback, so AUTO can resume
                if (post->liveCompareStatus().verdict != Verdict::accepted) break;
                end_match_test::toggle (*editor);
                require (! post->liveCompareStatus().matchOff && post->liveCompareStatus().gain == matchedGain
                    && post->liveCompareStatus().postTarget == matchedPost, "MATCH on: the same gain and lowered POST");
                require (end_match_test::autoOn (*editor, {}), "AUTO remains enabled after replay and MATCH on");
                chainGain.store (0.9f); stoppedAt = blocks.load();
                stage = 11;
                break;
            case 11:
                if (blocks.load() < stoppedAt + 26 || post->liveCompareStatus().postActual > matchedPost
                    || ! post->liveCompareStatus().matchReady) break;
                end_match_test::follow (*editor, post->liveCompareStatus());
                if (std::abs (post->liveCompareStatus().gain - matchedGain) < 0.01f) break;
                require (end_match_test::autoOn (*editor, {}), "AUTO actually moves PRE after replay and MATCH on");
                matchedGain = post->liveCompareStatus().gain;
                if (! capChecked)
                {   // MATCH on checks a kept boost against PRE's true peak now: capped at the ceiling, told as TP LIMIT.
                    auto& memory = LiveTimingFixtureAccess::matchMemory (*post);
                    require (post->setLiveCompareMatchOn (false).turned, "MATCH turns off before the ceiling check");
                    keptMatch = memory; memory.pre = 2.0f; memory.ceiling = std::pow (10.0f, -1.0f / 20.0f); memory.limited = false;
                    intersample.store (true); stoppedAt = blocks.load(); stage = 15; break;
                }
                stage = 5; // the same MATCH is ready again: continue with PRE
                break;
            case 15:
                if (blocks.load() < stoppedAt + 26) break;
                {
                    const auto evidence = post->measureLiveCompare();
                    if (! evidence.ok()) break;
                    const auto on = post->setLiveCompareMatchOn (true);
                    const auto now = post->liveCompareStatus();
                    const auto sampleDb = 20.0 * std::log10 (0.6 / std::sqrt (2.0));
                    const auto expected = -1.0 - evidence.prePeakDbtp;
                    require (evidence.prePeakDbtp > sampleDb + 1.0 && evidence.prePeakDbtp > keptMatch.peakDbtp + 0.5,
                             "current intersample true peak exceeds sample and kept peaks");
                    require (on.turned && now.matchLimited && std::abs (on.gainDb - expected) < 0.05
                        && std::abs (on.shortDb - (20.0 * std::log10 (2.0) - expected)) < 0.05
                        && std::abs (20.0 * std::log10 (now.gain) - expected) < 0.05,
                             "MATCH on caps at the current true peak and reports the short amount");
                    require (post->setLiveCompareMatchOn (false).turned, "MATCH turns off after the ceiling check");
                    LiveTimingFixtureAccess::matchMemory (*post) = keptMatch;
                    require (post->setLiveCompareMatchOn (true).turned, "the kept match returns");
                    intersample.store (false); chainGain.store (0.5f); stoppedAt = blocks.load(); capChecked = true; stage = 16;
                }
                break;
            case 16:
                if (blocks.load() >= stoppedAt + 26) stage = 5;
                break;
            default: break;
        }
    }
    float processed (float input) const
    { return mode == "restore-approval" ? -std::max (-0.8f, std::min (0.8f, input * 8.0f)) : input * -chainGain.load(); }
    void processAudio()
    {
        hypha::tests::ProductTestClock wallClock;
        juce::AudioBuffer<float> buffer (2, frames);
        juce::MidiBuffer midi;
        auto next = Steady::now();
        while (running.load())
        {
            if (pause.load())
            {
                paused.store (true);
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                next = Steady::now(); continue;
            }
            paused.store (false);
            clock.playing = playing.load();
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < frames; ++f)
                    buffer.setSample (c, f, intersample.load() ? (((clock.position + f) % 4 < 2) ? 0.42426407f : -0.42426407f)
                                                              : signal[static_cast<std::size_t> (clock.position + f) % signal.size()]);
            pre->processBlock (buffer, midi);
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < frames; ++f) buffer.setSample (c, f, processed (buffer.getSample (c, f)));
            post->processBlock (buffer, midi);
            if (verifyClose.exchange (false))
            {
                const auto first = signal[static_cast<std::size_t> (clock.position + 7) % signal.size()];
                const auto last = signal[static_cast<std::size_t> (clock.position + frames - 1) % signal.size()];
                if (! hypha::tests::finiteNear (buffer.getSample (0, 7), first, 0.03f)
                    || ! hypha::tests::finiteNear (buffer.getSample (0, frames - 1), processed (last), 1.0e-6f)) ++fadeErrors;
                ++fadeChecks;
            }
            if (verifyPost.load())
                for (int c = 0; c < 2; ++c)
                    for (int f = 0; f < frames; ++f)
                    {
                        const auto input = signal[static_cast<std::size_t> (clock.position + f) % signal.size()];
                        if (! hypha::tests::finiteNear (buffer.getSample (c, f), processed (input) * expectedPost.load(), 1.0e-6f))
                            postErrors.fetch_add (1);
                    }
            ratio.store (buffer.getSample (0, frames - 1)
                / signal[static_cast<std::size_t> (clock.position + frames - 1) % signal.size()]);
            if (clock.playing) clock.position += frames;
            blocks.fetch_add (1);
            next += std::chrono::nanoseconds (static_cast<long long> (frames) * 1'000'000'000 / 48000);
            wallClock.advanceBlock (frames, 48000);
            std::this_thread::sleep_until (next);
        }
    }
    static constexpr int frames = 8192;
    std::vector<float> signal;
    std::string mode;
    Clock clock;
    std::unique_ptr<Processor> pre, post;
    std::unique_ptr<KirinHyphaEditor> editor;
    ProcessorState::MatchMemory keptMatch;
    std::thread audio;
    hypha::pair_preview::Ticket preview;
    std::atomic<bool> running { true }, playing { false }, pause { false }, paused { false }, verifyPost { false };
    std::atomic<int> blocks { 0 }, postErrors { 0 };
    std::atomic<bool> verifyClose { false };
    std::atomic<int> fadeChecks { 0 }, fadeErrors { 0 };
    std::atomic<float> ratio { 0.0f }, expectedPost { 1.0f };
    std::atomic<float> chainGain { 0.5f };
    std::atomic<bool> intersample { false };
    Steady::time_point began = Steady::now(), requestedAt, checkedAt;
    int stage = 0;
    bool demanded = false;
    float held = 1.0f, matchedGain = 1.0f;
};
}

int main (int argc, char** argv)
{
    require (argc == 3, "usage: lifecycle S-1.wav scenario");
    auto signal = readFixture (argv[1]);
    ReferenceValidationStorageSandbox sandbox;
    // macOS JUCE resolves the home without HOME; keep PRE display files out of the real Kirin OS.
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    Lifecycle contract (std::move (signal), argv[2]);
    juce::MessageManager::getInstance()->runDispatchLoop();
    return contract.passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
