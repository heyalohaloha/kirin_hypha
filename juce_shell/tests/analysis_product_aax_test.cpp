#include "../src/PluginProcessor.h"
#include "../src/HyphaHostAvailability.h"
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaSpectrumComponent.h"
#include "LiveTimingFixtureAccess.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void require (bool ok, const char* message)
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
float sample (std::int64_t position, int channel)
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
            processor->prepareToPlay (48000, frames);
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
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    template <typename Check> void until (Check check, const char* why)
    {
        trace (why);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (12);
        do { block(); if (check()) return; } while (std::chrono::steady_clock::now() < deadline);
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
    // KIRIN_AAX_TEST_TRACE=1 prints every awaited step with the callback count, to place a failure.
    void trace (const char* why) const
    {
        if (std::getenv ("KIRIN_AAX_TEST_TRACE") != nullptr) std::cerr << "[" << callbacks << "] " << why << '\n';
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
        require (listen.loopBlocksPairing == expected, "LISTEN status carries the loop guidance only when a range loops");
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
        require (blind.loopBlocksPairing == expected
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
        psb.setPairingLoopBlocked (post->analysisPairingLoopBlocked());
        require ((psb.psbStatusText() == "Loop Playback on: turn it off to compare") == expected,
                 "the PSB difference asks for loop playback off exactly when the uncertified host loops a range");
    }
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
int main (int argc, char** argv)
try
{
    juce::ScopedJuceInitialiser_GUI initialise;
    require (std::getenv ("KIRIN_HYPHA_TEST_STORAGE_ROOT") != nullptr, "isolated fixture storage is required");
    const int channels = argc > 1 && juce::String (argv[1]) == "mono" ? 1 : 2;
    Fixture fixture (channels);

    // The measured (certified) host build. LIVE and the absolute PSB live on the project timeline
    // on every AAX build: an engine clock gap does not touch them.
    fixture.demand (hypha::analysis::Kind::liveAbsolute);
    fixture.preparingSoon (false);
    fixture.until ([&] { return fixture.absoluteActive(); }, "LIVE and absolute PSB use the project timeline without output latency");
    fixture.postClock.available = false;
    fixture.throughout (4, [&] { return fixture.absoluteActive(); }, "LIVE keeps the project timeline through an engine clock gap");
    fixture.postClock.available = true;
    fixture.until ([&] { return fixture.post->setPairCandidate (fixture.pre->instanceId(), {}); }, "exact PRE is selected");
    fixture.demand (hypha::analysis::Kind::sharpDelta);
    fixture.until ([&] { return fixture.sharpActive(); }, "certified SHARP and delta PSB must use the proven nonzero K");
    fixture.postClock.available = false;
    fixture.block(); fixture.preparing (true);
    fixture.postClock.available = true;
    fixture.until ([&] { return fixture.sharpActive(); }, "fresh proof reacquires analysis");
    fixture.preClock.project += 4096; fixture.postClock.project += 4096;
    fixture.sharpReproves ("seek renews the proof before SHARP returns");
    fixture.post->kirinHostDelayCompensationStateChanged (false);
    fixture.block(); fixture.preparing (true);
    fixture.post->kirinHostDelayCompensationStateChanged (true);
    fixture.until ([&] { return fixture.sharpActive(); }, "compensation ON renews the pair proof");
    fixture.block (true); fixture.preparing (true);
    fixture.until ([&] { return fixture.sharpActive(); }, "other output owner leaving renews the pair proof");

    // An AAX host that is not Pro Tools: no certificate, no documented bound. SHARP and the delta
    // PSB use the same linear PRE/POST proof as LISTEN; they are never refused as missing output
    // latency.
    fixture.certified (false);
    fixture.sharpReproves ("an AAX host that is not Pro Tools proves SHARP through LISTEN's linear K");
    require (! fixture.sharpRefused(), "an AAX host that is not Pro Tools is not a missing-latency host");
    require (! fixture.post->analysisPairingLoopBlocked(), "linear playback asks for no loop change");
    fixture.loopGuidance (false);
    fixture.preClock.project += 4096; fixture.postClock.project += 4096;
    fixture.sharpReproves ("not Pro Tools: seek renews the proof before SHARP returns");
    fixture.post->kirinHostDelayCompensationStateChanged (false);
    fixture.block(); fixture.preparing (true);
    fixture.post->kirinHostDelayCompensationStateChanged (true);
    fixture.until ([&] { return fixture.sharpActive(); }, "not Pro Tools: compensation ON renews the pair proof");
    fixture.playing (false);
    fixture.block(); fixture.preparing (true);
    fixture.playing (true);
    fixture.until ([&] { return fixture.sharpActive(); }, "not Pro Tools: play after stop renews the pair proof");
    fixture.postClock.available = false;
    fixture.block(); fixture.preparing (true);
    fixture.postClock.available = true;
    fixture.until ([&] { return fixture.sharpActive(); }, "not Pro Tools: an engine clock gap revokes SHARP until proven again");

    // The host's loop-playback setting with no selection names no loop: playback is linear.
    fixture.loopPlayback (true);
    fixture.throughout (40, [&] { return fixture.sharpActive() && ! fixture.post->analysisPairingLoopBlocked(); },
                        "loop playback without a range is linear playback");
    // A looped range needs Pro Tools' documented bound; here only the guidance appears while
    // POST waits, even though the host really wraps both sides.
    fixture.loopRange (2.0);
    fixture.block(); fixture.preparing (true);
    fixture.throughout (120, [&] { return ! fixture.sharpActive() && fixture.analysisPreparing (true)
                                       && fixture.post->analysisPairingLoopBlocked(); },
                        "an AAX host that is not Pro Tools looping a range waits with loop guidance and no difference");
    fixture.loopGuidance (true);
    fixture.demand (hypha::analysis::Kind::liveAbsolute);
    fixture.until ([&] { return fixture.absoluteActive(); }, "LIVE and absolute PSB ignore the loop setting");
    KirinPsbView psb {};
    require (fixture.post->pollPsb (psb) && psb.is_delta == 0, "project PSB contains only POST facts");
    fixture.demand (hypha::analysis::Kind::sharpDelta);
    fixture.loopPlayback (false);
    fixture.until ([&] { return fixture.sharpActive() && ! fixture.post->analysisPairingLoopBlocked(); },
                   "loop playback off restores the linear proof and clears the guidance");
    fixture.loopGuidance (false);

    // A regular Pro Tools build (certified by the version floor, not the measured one): the
    // linear proof as before, and in a looped range the bounded initial-loop K, so SHARP, the
    // delta PSB and LISTEN all proceed while the host wraps both sides. No loop guidance appears.
    fixture.certified (true, "26.4.1.179");
    fixture.sharpReproves ("a regular Pro Tools build proves SHARP through the linear K");
    // Pro Tools wraps the transport once per cycle for the whole chain, so POST's compensated
    // position lies before the loop start for K samples after every wrap. Two beats are 187
    // callbacks: 400 callbacks cross at least two wraps.
    fixture.compensatedPositionWrapsWithEngine = true;
    fixture.loopRange (2.0);
    fixture.until ([&] { return fixture.sharpActive(); },
                   "a regular Pro Tools build proves SHARP and the delta PSB through a looped range with K");
    fixture.throughout (400, [&] { return fixture.sharpActive() && ! fixture.post->analysisPairingLoopBlocked(); },
                        "SHARP stays proven and exact across the loop wraps without asking for loop playback off");
    fixture.loopGuidance (false);
    require (fixture.post->startLiveCompare() == hypha::live_compare::StartResult::started,
             "LISTEN starts on a regular Pro Tools build during loop playback");
    fixture.post->selectLiveComparePre (true);
    fixture.until ([&] { const auto status = fixture.post->liveCompareStatus();
                         return status.active && status.preSelected && status.preAudible
                             && status.verdict == hypha::live_compare::Verdict::accepted; },
                   "LISTEN plays PRE through a looped range on a regular Pro Tools build");
    fixture.throughout (400, [&] { const auto status = fixture.post->liveCompareStatus();
                                   return status.preAudible && ! status.contentHeld && ! status.loopBlocksPairing; },
                        "PRE stays audible across the loop wraps without a hold or loop guidance");
    fixture.post->stopLiveCompare();
    fixture.loopPlayback (false);
    fixture.until ([&] { return fixture.sharpActive(); }, "a regular Pro Tools build returns to the linear proof");
    // A loop no longer than the compensation bound (0.6 beats are 14400 samples, below 16383):
    // a K already proven carries through it exactly, but once lost it cannot be proven again
    // there, so SHARP waits with no difference rather than pairing a wrong lap.
    fixture.loopRange (0.6);
    fixture.until ([&] { return fixture.sharpActive(); }, "a known K is corroborated by the first wrap of a loop no longer than the bound");
    fixture.throughout (200, [&] { return fixture.sharpActive(); },
                        "a known K carries exactly through a loop no longer than the bound");
    fixture.postClock.available = false;
    fixture.block(); fixture.preparing (true);
    fixture.postClock.available = true;
    fixture.throughout (200, [&] { return ! fixture.sharpActive() && fixture.analysisPreparing (true); },
                        "a loop no longer than the compensation bound proves no new K: no difference, no wrong lap");
    fixture.loopPlayback (false);
    fixture.until ([&] { return fixture.sharpActive(); }, "linear playback after a short loop proves SHARP again");
    // A loop just longer than the bound (0.71 beats are 17040 samples) proves a new K with K near
    // a quarter of the loop, and pairs the right lap across its frequent wraps.
    fixture.loopRange (0.71);
    fixture.postClock.available = false;
    fixture.block(); fixture.preparing (true);
    fixture.postClock.available = true;
    fixture.until ([&] { return fixture.sharpActive(); }, "a loop just longer than the bound proves a new K with K");
    fixture.throughout (300, [&] { return fixture.sharpActive(); }, "the short loop stays exact across its wraps");
    fixture.loopPlayback (false);
    fixture.until ([&] { return fixture.sharpActive(); }, "linear playback after the short loop proves SHARP again");

    // A host that wraps each insert's compensated position at the loop end proves the same way.
    fixture.compensatedPositionWrapsWithEngine = false;
    fixture.postClock.project = fixture.preClock.project - delay;
    fixture.loopRange (2.0);
    fixture.until ([&] { return fixture.sharpActive(); }, "a per-insert wrap proves SHARP through a looped range with K");
    fixture.throughout (400, [&] { return fixture.sharpActive(); }, "a per-insert wrap stays exact across the loop wraps");
    fixture.loopPlayback (false);
    fixture.until ([&] { return fixture.sharpActive(); }, "linear playback after a per-insert wrap proves SHARP again");

    // The measured build in the same looped range proves the same way.
    fixture.certified (true);
    fixture.compensatedPositionWrapsWithEngine = true;
    fixture.loopRange (2.0);
    fixture.until ([&] { return fixture.sharpActive(); }, "the measured build proves SHARP through a looped range");
    fixture.throughout (400, [&] { return fixture.sharpActive() && ! fixture.post->analysisPairingLoopBlocked(); },
                        "a certified host never asks to turn loop playback off");
    fixture.loopGuidance (false);
    std::cout << "AAX analysis product: PASS (" << channels << " channels)\n";
}
catch (const std::exception& error)
{
    std::cerr << "AAX analysis: " << error.what() << '\n';
    return 1;
}
