#pragma once
#include "../src/PluginProcessor.h"
#include "../src/HyphaHostAvailability.h"
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaSpectrumComponent.h"
#include "LiveTimingFixtureAccess.h"
#include "ProductTestClock.h"
#include "AnalysisProductWait.h"
#include "AnalysisWorkerReceipt.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "TestProcessorStorage.h"
namespace aax_analysis_product
{
static void require (bool ok, const char* message)
{
    if (! ok) throw std::runtime_error (message);
}
// A synthetic AAX host: engine clock (AddClock), project timeline, and the loop-playback setting
// with the selection it would loop. 120 BPM at 48 kHz: one beat is 24000 samples.
struct Clock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setTimeInSamples (project);
        info.setPpqPosition (static_cast<double> (project) / 24000.0);
        info.setBpm (120.0);
        info.setKirinAuxiliaryClockSource (3);
        if (available) info.setKirinAuxiliaryClockSamples (engine);
        if (loopPlayback)
        {
            info.setIsLooping (true);
            info.setLoopPoints (LoopPoints { loopStartPpq, loopEndPpq });
        }
        return info;
    }
    // One callback later: the engine clock always advances; the project position wraps from the
    // loop end to the loop start when loop playback loops a range, as the host's does.
    void advance (int frames)
    {
        engine += frames; project += frames;
        if (! loopPlayback || loopEndPpq <= loopStartPpq) return;
        const auto start = static_cast<std::int64_t> (loopStartPpq * 24000.0);
        const auto end = static_cast<std::int64_t> (loopEndPpq * 24000.0);
        if (project >= end) project = start + (project - end);
    }
    std::int64_t project = 0, engine = 960000;
    bool playing = true, available = true, loopPlayback = false;
    double loopStartPpq = 0.0, loopEndPpq = 0.0;
};
using Processor = KirinHyphaProcessorBase;
constexpr int frames = 256;
constexpr int delay = 4096;
static float sample (std::int64_t position, int channel)
{
    return static_cast<float> (0.1 * std::sin (position * (channel == 0 ? 0.13 : 0.17)));
}
struct Fixture
{
    explicit Fixture (int channelsIn) : channels (channelsIn)
    {
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AAX);
            auto processor = std::make_unique<Processor> (role);
            auto layout = processor->getBusesLayout();
            const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            layout.inputBuses.set (0, set); layout.outputBuses.set (0, set);
            require (processor->setBusesLayout (layout), "mono/stereo negotiates");
            processor->setPlayHead (role == Processor::Role::Pre ? &preClock : &postClock);
            // Pro Tools names each instance's group before the first prepare (INV-LC9). A mono
            // track's instance is alone in its group; without this a mono PRE publishes a
            // multi-mono timing ring and POST can never prove the pair for SHARP / delta PSB.
            processor->kirinHostInstanceGroup (role == Processor::Role::Pre ? 1u : 2u, true);
            hypha::tests::prepareFixtureProcessor (*processor, 48000, frames);
            LiveTimingFixtureAccess::configureAaxClock (*processor);
            LiveTimingFixtureAccess::enableFixtureWrites (*processor);
            (role == Processor::Role::Pre ? pre : post) = std::move (processor);
        }
        postClock.project = -delay;
        // POST hears PRE's audio K samples later, whatever position the host names for it.
        delayLine.resize (static_cast<std::size_t> (channels));
        for (int ch = 0; ch < channels; ++ch)
            for (int i = -delay; i < 0; ++i) delayLine[static_cast<std::size_t> (ch)].push_back (sample (i, ch));
        require (LiveTimingFixtureAccess::timingRingPublished (*pre),
                 "fixture requires the native PRE timing shared-memory mapping");
        owner = post->beginAnalysisUiSession();
        require (owner != 0, "POST owns optional analysis");
    }
    ~Fixture()
    {
        post->endAnalysisUiSession (owner);
        post->releaseResources(); pre->releaseResources();
    }
    // qualified: a Pro Tools build (the measured one, or any other build by its version string);
    // otherwise an AAX host that is not Pro Tools.
    void certified (bool qualified, const char* version = "26.4.0.5")
    {
        LiveTimingFixtureAccess::configureAaxClock (*pre, qualified, version);
        LiveTimingFixtureAccess::configureAaxClock (*post, qualified, version);
    }
    void playing (bool on) { preClock.playing = postClock.playing = on; }
    void loopPlayback (bool on, double startPpq = 0.0, double endPpq = 0.0)
    {
        for (auto* clock : { &preClock, &postClock })
        {
            clock->loopPlayback = on;
            clock->loopStartPpq = startPpq; clock->loopEndPpq = endPpq;
        }
        // One host timeline: leaving the loop, POST's position is PRE's minus K, never another lap.
        if (! on) postClock.project = preClock.project - delay;
    }
    // Loop playback over a selection of `beats` that contains both positions now, so the host
    // really wraps both PRE and POST (POST wraps K samples after PRE). Two beats at 120 BPM are
    // 48000 samples: longer than the 16383-sample compensation bound the proof needs.
    void loopRange (double beats)
    {
        const auto first = std::min (preClock.project, postClock.project);
        const auto startBeat = std::floor (static_cast<double> (first) / 24000.0);
        loopPlayback (true, startBeat, startBeat + beats);
        // Until PRE's first wrap the loop has no measured cycle, so a proof waits one lap; the
        // checks that follow look at the loop as it runs, not at that first lap.
        for (int i = 0, lap = static_cast<int> (beats * 24000.0) / frames + 8; i < lap; ++i) block();
    }
    void demand (hypha::analysis::Kind kind)
    {
        hypha::analysis::Demand demand;
        demand.kind = kind;
        require (post->setAnalysisDemand (owner, demand), "analysis demand is accepted");
    }
    void block (bool otherOutputOwner = false)
    {
        for (auto* processor : { pre.get(), post.get() })
        {
            juce::AudioBuffer<float> audio (channels, frames), input (channels, frames);
            for (int ch = 0; ch < channels; ++ch)
            {
                auto& line = delayLine[static_cast<std::size_t> (ch)];
                for (int i = 0; i < frames; ++i)
                {
                    float value = 0.0f;
                    if (processor == pre.get()) { value = sample (preClock.project + i, ch); line.push_back (value); }
                    else { value = line.front(); line.pop_front(); }
                    audio.setSample (ch, i, value); input.setSample (ch, i, value);
                }
            }
            juce::MidiBuffer midi;
            if (otherOutputOwner && processor == post.get())
                LiveTimingFixtureAccess::processAaxAnalysisWithOtherOutputOwner (*processor, audio);
            else
                processor->processBlock (audio, midi);
            hypha::tests::awaitAnalysisInput (*processor);
            // The explicit LISTEN audition may replace POST's output with PRE; ordinary A stays exact.
            const bool auditioning = processor == post.get() && post->liveCompareStatus().active;
            for (int ch = 0; ch < channels && ! auditioning; ++ch)
                for (int i = 0; i < frames; ++i)
                {
                    const auto actual = audio.getSample (ch, i), expected = input.getSample (ch, i);
                    require (std::memcmp (&actual, &expected, sizeof (float)) == 0, "normal A is bit identical");
                }
            require (processor->getLatencySamples() == 0, "normal A latency is zero");
        }
        // Whenever POST holds a pair proof, its paired coordinate must name the PRE callback of
        // the same audio: K samples before PRE's own clock of this callback, never another lap.
        const auto preLocal = LiveTimingFixtureAccess::aaxLocalClock (*pre);
        const auto paired = LiveTimingFixtureAccess::aaxPairedClock (*post);
        if (std::getenv ("KIRIN_AAX_TEST_TRACE") != nullptr && paired.valid != pairedWasValid)
            std::cerr << "[" << callbacks << "] paired " << (paired.valid ? "proven" : "lost") << " PRE project "
                      << preClock.project << " POST project " << postClock.project << " engine " << preClock.engine
                      << " loop " << preClock.loopPlayback << " " << preClock.loopStartPpq << ".." << preClock.loopEndPpq << '\n';
        pairedWasValid = paired.valid;
        if (preLocal.valid && paired.valid && preLocal.start - paired.start != delay)
        {
            std::cerr << "pair proof K=" << (preLocal.start - paired.start) << " expected " << delay
                      << " at PRE project " << preClock.project << " POST project " << postClock.project << '\n';
            require (false, "the paired coordinate names the same audio K samples earlier, not another lap");
        }
        preClock.advance (frames);
        if (compensatedPositionWrapsWithEngine)
        {
            // The host's transport wraps once for the whole chain and POST's compensated position
            // is that position minus K: for K samples after each wrap it lies before the loop start.
            postClock.engine += frames;
            postClock.project = preClock.project - delay;
        }
        else postClock.advance (frames);
        LiveTimingFixtureAccess::serviceTiming (*post);
        ++callbacks;
        wallClock.advanceBlock (frames, 48000);
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    template <typename Check> void until (Check check, const char* why)
    {
        trace (why);
        const auto result = hypha::tests::waitForAnalysis (
            hypha::tests::analysisWaitBlocks (48000, frames), phaseDeadline,
            [&] { block(); }, check, [] { return std::chrono::steady_clock::now(); });
        if (result == hypha::tests::AnalysisWaitResult::ready) return;
        KirinPerceptualBatch perceptual {}; KirinAbsoluteBatch absolute {}; KirinPsbView psb {};
        post->pollPerceptualBatch (perceptual); post->pollAbsoluteBatch (absolute); post->pollPsb (psb);
        const juce::String message = juce::String (why) + " [SHARP status " + juce::String (perceptual.latest.status)
            + " data " + juce::String (perceptual.latest.has_data) + " delta " + juce::String (perceptual.latest.delta_sharpness, 4)
            + ", LIVE status " + juce::String (absolute.latest.status) + ", PSB status " + juce::String (psb.status)
            + " delta " + juce::String (psb.is_delta) + "]";
        throw std::runtime_error (message.toStdString());
    }
    template <typename Check> void throughout (int blocks, Check check, const char* why)
    {
        trace (why);
        for (int i = 0; i < blocks; ++i)
        {
            block();
            if (check()) continue;
            // A poll can meet the exchange worker's lock; the editor skips that tick, so may this.
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
            if (check()) continue;
            KirinPerceptualBatch perceptual {}; KirinPsbView psb {};
            post->pollPerceptualBatch (perceptual); post->pollPsb (psb);
            const juce::String message = juce::String (why) + " [block " + juce::String (i) + ", SHARP status "
                + juce::String (perceptual.latest.status) + " data " + juce::String (perceptual.latest.has_data)
                + " delta " + juce::String (perceptual.latest.delta_sharpness, 4) + ", PSB status " + juce::String (psb.status)
                + ", loop guidance " + juce::String (static_cast<int> (post->analysisPairingLoopBlocked())) + "]";
            throw std::runtime_error (message.toStdString());
        }
    }
    bool absoluteActive()
    {
        KirinAbsoluteBatch batch {};
        KirinPsbView psb {};
        return post->pollAbsoluteBatch (batch) && batch.latest.status == KIRIN_SPECTRUM_ACTIVE
            && batch.latest.has_data != 0 && post->pollPsb (psb) && psb.has_data != 0;
    }
    bool sharpActive()
    {
        KirinPerceptualBatch batch {};
        KirinPsbView psb {};
        return post->pollPerceptualBatch (batch) && batch.latest.status == KIRIN_SPECTRUM_ACTIVE
            && batch.latest.has_data != 0 && std::abs (batch.latest.delta_sharpness) < 1.0e-6
            && post->pollPsb (psb) && psb.has_data != 0 && psb.is_delta != 0;
    }
    bool sharpRefused()
    {
        KirinPerceptualBatch batch {};
        return post->pollPerceptualBatch (batch)
            && batch.latest.status == KIRIN_SPECTRUM_OUTPUT_LATENCY_UNAVAILABLE;
    }
    bool analysisPreparing (bool sharp)
    {
        KirinPerceptualBatch perceptual {}; KirinAbsoluteBatch absolute {};
        return sharp ? post->pollPerceptualBatch (perceptual)
                        && perceptual.latest.status == KIRIN_SPECTRUM_WARMING_UP && perceptual.latest.has_data == 0
                     : post->pollAbsoluteBatch (absolute)
                        && absolute.latest.status == KIRIN_SPECTRUM_WARMING_UP && absolute.latest.has_data == 0;
    }
    bool psbPreparing()
    {
        KirinPsbView psb {};
        return post->pollPsb (psb) && psb.status == KIRIN_SPECTRUM_WARMING_UP && psb.has_data == 0;
    }
    void preparing (bool sharp)
    {
        if (analysisPreparing (sharp) && psbPreparing()) return;
        std::this_thread::sleep_for (std::chrono::milliseconds (2)); // one contended poll is not a fact
        if (analysisPreparing (sharp) && psbPreparing()) return;
        KirinPerceptualBatch perceptual {}; KirinAbsoluteBatch absolute {}; KirinPsbView psb {};
        post->pollPerceptualBatch (perceptual); post->pollAbsoluteBatch (absolute); post->pollPsb (psb);
        const juce::String message = juce::String ("AAX waits for fresh proof and analysis without stale facts [callback ")
            + juce::String (callbacks) + ", SHARP status " + juce::String (perceptual.latest.status) + " data "
            + juce::String (perceptual.latest.has_data) + ", LIVE status " + juce::String (absolute.latest.status)
            + " data " + juce::String (absolute.latest.has_data) + ", PSB status " + juce::String (psb.status) + "]";
        throw std::runtime_error (message.toStdString());
    }
    // Always flush awaited steps so a CTest timeout identifies the last completed transition.
    void trace (const char* why) const
    {
        std::cerr << "[" << callbacks << "] " << why << std::endl;
    }
    // A new demand stores HIDDEN until the POST IO thread's next 10 Hz tick starts the exchange
    // worker, which publishes the first WARMING_UP. A UI poll in that window sees HIDDEN; this is
    // not a stale fact, so the first status after a demand is awaited rather than read at once.
    void preparingSoon (bool sharp)
    {
        until ([&] { return analysisPreparing (sharp) && psbPreparing(); },
               "AAX presents WARMING_UP for the new demand instead of stale facts or an unsupported host");
    }
    // The proof is lost on this block; the next blocks prove it again and SHARP returns.
    void sharpReproves (const char* why)
    {
        block(); preparing (true);
        until ([&] { return sharpActive(); }, why);
    }
    // The guidance the three surfaces give in the current host state: LISTEN's footer, Blind's
    // screen and the PSB difference. They ask for loop playback off together, or none of them does,
    // and the host boundary "cannot verify timing" never covers a loop that can be turned off.
    void loopGuidance (bool expected)
    {
        using namespace hypha::live_compare_ui;
        using hypha::live_compare::BlindStage;
        auto listen = post->liveCompareStatus();
        require ((listen.loopBlock == hypha::live_compare::LoopPairingBlock::aaxLoop) == expected, "LISTEN status carries the loop guidance only when a range loops");
        // As during a session on this host: PRE selected and waiting on an unidentifiable loop.
        listen.active = listen.preSelected = listen.preWaiting = true;
        listen.observation = hypha::live_compare::RecoveryReason::loopClockUnavailable;
        listen.timingUnavailable = true;
        const auto notice = currentNamedPresentation (listen);
        require ((notice.action == RecoveryAction::loopOff) == expected
            && (juce::String (notice.instruction) == "Loop Playback on: turn it off to compare (POST)") == expected
            && (juce::String (notice.instruction) == hypha::host_availability::timingText()) != expected,
            "LISTEN asks for loop playback off exactly when the uncertified host loops a range");
        LiveTimingFixtureAccess::blindWaiting (*post, BlindStage::preparing);
        auto blind = post->liveBlindStatus();
        LiveTimingFixtureAccess::blindWaiting (*post, BlindStage::idle);
        blind.stage = BlindStage::preparing;
        blind.observation = hypha::live_compare::RecoveryReason::loopClockUnavailable;
        blind.timingUnavailable = true;
        const auto screen = hypha::live_blind_ui::screenFor (blind, true, 1.0f);
        require ((blind.loopBlock == hypha::live_compare::LoopPairingBlock::aaxLoop) == expected
            && (blindRecovery (blind, false).action == RecoveryAction::loopOff) == expected
            && (screen.instruction == "Loop Playback on: turn it off to compare") == expected
            && (screen.cause == hypha::host_availability::timingText()) != expected,
            "Blind asks for loop playback off exactly when the uncertified host loops a range");
        KirinPsbView frame {};
        if (! post->pollPsb (frame)) // one contended poll is not a fact; the editor skips that tick
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
            require (post->pollPsb (frame), "the PSB frame is published");
        }
        hypha::SpectrumComponent psb;
        psb.setAbsoluteObservation (false); psb.setSignalActive (true);
        psb.setPsbSnapshot (frame);
        psb.setPairingLoopBlock (post->analysisPairingLoopBlock());
        require ((psb.psbStatusText() == "Loop Playback on: turn it off to compare") == expected,
                 "the PSB difference asks for loop playback off exactly when the uncertified host loops a range");
    }
    // A real watchdog bounds a stuck worker; it does not decide the amount of audio supplied.
    static constexpr int phaseTimeoutSeconds = 90;
    const std::chrono::steady_clock::time_point phaseDeadline = hypha::tests::analysisPhaseDeadline (
        std::chrono::steady_clock::now(), phaseTimeoutSeconds);
    hypha::tests::ProductTestClock wallClock;
    int channels;
    int callbacks = 0;
    bool pairedWasValid = false;
    bool compensatedPositionWrapsWithEngine = false;
    std::vector<std::deque<float>> delayLine; // PRE's audio on its way to POST, K samples long
    Clock preClock, postClock;
    std::unique_ptr<Processor> pre, post;
    std::uint64_t owner = 0;
};
}
