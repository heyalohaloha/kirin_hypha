#include "../src/PluginProcessor.h"
#include "ValidationStorageSandbox.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

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
            instance->prepareToPlay (48000, frames);
            (role == Processor::Role::Pre ? pre : post) = std::move (instance);
        }
        audio = std::thread ([this] { processAudio(); });
        startTimer (20);
    }
    ~Lifecycle() override { stopTimer(); running.store (false); audio.join(); }
    bool passed = false;

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
    void restore (bool endRequested = false)
    {
        pause.store (true);
        waitWithoutMessageService ([this] { return paused.load(); });
        held = post->liveCompareStatus().postActual;
        const auto approval = post->liveBlindStatus().generation;
        const auto oldMatch = post->measureLiveCompare();
        juce::MemoryBlock saved;
        post->getStateInformation (saved);
        if (endRequested) post->finishLiveCompare();
        std::thread host ([&]
        {
            if (mode == "restore-invalid") post->setStateInformation (nullptr, 0);
            else post->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        });
        host.join();
        require (! post->revealLiveBlind() && ! post->selectLiveBlind (1), "old trial controls rejected before service");
        require (! post->liveCompareStatus().matched && ! post->liveCompareStatus().active,
                 "MATCH and output permission immediately revoked");
        require (! post->liveBlindStatus().trial.revealed && post->liveBlindStatus().trial.played == 0,
                 "mapping and played receipts immediately hidden");
        if (oldMatch.ok()) require (! post->applyLiveCompareMatch (planMatch (oldMatch, 0.0), MatchChoice::basis),
                                   "old measurement rejected after restore");
        const auto before = blocks.load();
        expectedPost.store (held);
        verifyPost.store (! endRequested);
        pause.store (false);
        waitWithoutMessageService ([&] { return blocks.load() > before; });
        require (postErrors.load() == 0, "first buffer is held POST only without message service");
        if (! endRequested) require (std::abs (post->liveCompareStatus().postActual - held) <= 0.0f, "restore never raises POST");
        require (! post->approveLiveBlindMatch (approval), "old approval rejected");
        if (endRequested) require (post->liveCompareStatus().finishing || post->liveCompareStatus().postActual == 1.0f,
                                  "explicit END survives until actual unity");
        checkedAt = Steady::now();
    }
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
        waitWithoutMessageService ([this] { return ! post->liveCompareStatus().finishing; }, 1500);
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
                if (pre->instanceId().isEmpty()) break;
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
                    require (post->applyLiveCompareMatch (planMatch (measured, -8.0), MatchChoice::basis), "full same-session MATCH");
                    stage = 5;
                }
                break;
            case 5:
                if (! post->liveCompareStatus().matchReady) break;
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
                restore(); stage = 6;
                break;
            case 4:
            {
                post->serviceLiveBlind();
                const auto status = post->liveBlindStatus();
                if (mode == "restore-approval")
                {
                    if (status.stage != BlindStage::approval) break;
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
                require (post->beginLiveBlind() == StartResult::started, "new explicit session starts after RETURN");
                post->closeLiveBlind();
                std::cout << "Live lifecycle: PASS " << mode << " postErrors=" << postErrors.load() << '\n';
                passed = true;
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
            default: break;
        }
    }
    float processed (float input) const
    { return mode == "restore-approval" ? -std::max (-0.8f, std::min (0.8f, input * 8.0f)) : input * -0.5f; }
    void processAudio()
    {
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
                    buffer.setSample (c, f, signal[static_cast<std::size_t> (clock.position + f) % signal.size()]);
            pre->processBlock (buffer, midi);
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < frames; ++f) buffer.setSample (c, f, processed (buffer.getSample (c, f)));
            post->processBlock (buffer, midi);
            if (verifyPost.load())
                for (int c = 0; c < 2; ++c)
                    for (int f = 0; f < frames; ++f)
                    {
                        const auto input = signal[static_cast<std::size_t> (clock.position + f) % signal.size()];
                        if (std::abs (buffer.getSample (c, f) - processed (input) * expectedPost.load()) > 1.0e-6f)
                            postErrors.fetch_add (1);
                    }
            ratio.store (buffer.getSample (0, frames - 1)
                / signal[static_cast<std::size_t> (clock.position + frames - 1) % signal.size()]);
            if (clock.playing) clock.position += frames;
            blocks.fetch_add (1);
            next += std::chrono::nanoseconds (static_cast<long long> (frames) * 1'000'000'000 / 48000);
            std::this_thread::sleep_until (next);
        }
    }
    static constexpr int frames = 8192;
    std::vector<float> signal;
    std::string mode;
    Clock clock;
    std::unique_ptr<Processor> pre, post;
    std::thread audio;
    hypha::pair_preview::Ticket preview;
    std::atomic<bool> running { true }, playing { false }, pause { false }, paused { false }, verifyPost { false };
    std::atomic<int> blocks { 0 }, postErrors { 0 };
    std::atomic<float> ratio { 0.0f }, expectedPost { 1.0f };
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
    ValidationStorageSandbox sandbox;
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
