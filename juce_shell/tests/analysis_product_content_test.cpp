#include "../src/PluginProcessor.h"
#include "LiveTimingFixtureAccess.h"
#include "ReferenceValidationStorageSandbox.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaLiveBlindComponent.h"
#include "pdc_validation_delay/FixedValidationDelay.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void require (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); }
using Processor = KirinHyphaProcessorBase;
constexpr int frames = 256;
struct Clock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        if (positionAvailable) info.setTimeInSamples (content);
        info.setKirinAuUsesHostTransportTimeline (projectWitness);
        info.setKirinAuxiliaryClockSource (audioUnit ? 2 : 1);
        if (available && ! reaper) info.setKirinAuxiliaryClockSamples (audioUnit ? content + inputDelay : content);
        info.setKirinPresentationLatencySource (audioUnit ? 2 : 1);
        if (reported) info.setKirinOutputPresentationLatencySamples (latency);
        if (loop)
        {
            constexpr std::int64_t length = 96000;
            const auto folded = hypha::live_compare::positiveModulo (content, length);
            if (positionAvailable) info.setTimeInSamples (folded);
            info.setIsLooping (true);
            info.setLoopPoints (LoopPoints { 0, length / (rate / 2.0) });
            info.setBpm (120);
            info.setPpqPosition (folded / (rate / 2.0));
            // The upstream node wraps first; the host clamps POST while its PPQ keeps the tail.
            const auto raw = (content + inputDelay) % length - inputDelay;
            if (! audioUnit && ! reaper && raw < 0 && content + inputDelay >= length)
            {
                info.setTimeInSamples (0);
                info.setPpqPosition (raw / 24000.0);
            }
        }
        return info;
    }
    std::int64_t content = 0, inputDelay = 0, latency = 512;
    double rate = 48000;
    bool reaper = false, positionAvailable = true;
    bool audioUnit = false, projectWitness = true;
    bool reported = true, available = true, playing = true, loop = false;
};

struct Fixture
{
    Fixture (int channelsIn, const juce::String& scenario, bool au, bool reaperIn) : channels (channelsIn), reaper (reaperIn)
    {
        preClock.audioUnit = postClock.audioUnit = au;
        preClock.reaper = postClock.reaper = reaper;
        preClock.rate = postClock.rate = rate = reaper ? 96000 : 48000;
        const bool zero = scenario == "zero";
        delay = zero ? 0 : hypha::pdc_validation::FixedValidationDelay::latencySamples;
        line.prepare (channels);
        preClock.reported = postClock.reported = ! reaper && scenario != "missing";
        if (scenario == "included") preClock.latency += delay;
        postClock.content = -delay;
        postClock.inputDelay = delay;
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (au ? juce::AudioProcessor::wrapperType_AudioUnit
                                                         : juce::AudioProcessor::wrapperType_VST3);
            auto processor = std::make_unique<Processor> (role);
            auto layout = processor->getBusesLayout();
            const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            layout.inputBuses.set (0, set); layout.outputBuses.set (0, set);
            require (processor->setBusesLayout (layout), "fixture layout");
            processor->setPlayHead (role == Processor::Role::Pre ? &preClock : &postClock);
            processor->prepareToPlay (rate, frames);
            if (! au) require (LiveTimingFixtureAccess::configureVst3Clock (*processor,
                reaper ? "REAPER" : "Studio Pro", reaper ? "7.71.0_ba637e1u" : "8.1.2.113407"), "host content certificate");
            LiveTimingFixtureAccess::enableFixtureWrites (*processor);
            (role == Processor::Role::Pre ? pre : post) = std::move (processor);
        }
        require (LiveTimingFixtureAccess::timingRingPublished (*pre), "PRE timing shared memory is required");
        owner = post->beginAnalysisUiSession();
        require (owner != 0, "analysis session");
    }
    ~Fixture()
    {
        post->endAnalysisUiSession (owner);
        post->releaseResources(); pre->releaseResources();
    }
    void process (Processor& processor, juce::AudioBuffer<float>& audio)
    {
        juce::AudioBuffer<float> original; original.makeCopyOf (audio);
        juce::MidiBuffer midi; processor.processBlock (audio, midi);
        for (int ch = 0; ch < channels; ++ch)
            if (auditionAllowed && &processor == post.get())
            {
                for (int i = 0; i < frames; ++i)
                    require (std::abs (audio.getSample (ch, i) - original.getSample (ch, i)) < 1.0e-6f,
                             "verified PRE audition matches the physical-delay input, including its fade");
            }
            else require (std::memcmp (audio.getReadPointer (ch), original.getReadPointer (ch),
                                      frames * sizeof (float)) == 0, "normal A must remain bit identical");
        require (processor.getLatencySamples() == 0, "zero latency");
    }
    void block()
    {
        juce::AudioBuffer<float> audio (channels, frames);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < frames; ++i)
            {
                const auto t = (preClock.content + i) / rate;
                audio.setSample (ch, i, static_cast<float> (0.12 * std::sin (t * (628 + ch * 77))
                    + (0.08 + 0.07 * std::sin (t * 14.45)) * std::sin (t * 27000)));
            }
        process (*pre, audio);
        if (delay != 0) require (line.process (audio.getArrayOfWritePointers(), channels, frames), "physical delay");
        process (*post, audio);
        preClock.content += frames; postClock.content += frames;
        LiveTimingFixtureAccess::serviceTiming (*post);
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    template <class Check> void until (Check check, const char* why)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds (12);
        do { block(); if (check()) return; } while (std::chrono::steady_clock::now() < end);
        require (false, why);
    }
    bool zeroDelta (bool spectrum)
    {
        if (spectrum)
        {
            KirinSpectrumBatch batch {};
            if (! post->pollSpectrumBatch (batch) || batch.latest.has_data == 0) return false;
            for (auto value : batch.latest.display_db) require (std::abs (value) < 1.0e-5f, "same-content FREQ delta");
            return true;
        }
        KirinPerceptualBatch batch {}; KirinPsbView psb {};
        if (! post->pollPerceptualBatch (batch) || batch.latest.has_data == 0
            || ! post->pollPsb (psb) || psb.has_data == 0) return false;
        require (std::abs (batch.latest.delta_sharpness) < 1.0e-8, "same-content SHARP delta");
        require (psb.is_delta != 0, "PSB is delta");
        for (auto value : psb.shares) require (std::abs (value) < 1.0e-8, "same-content PSB delta");
        return true;
    }
    void noDelta (bool spectrum)
    {
        KirinSpectrumBatch freq {}; KirinPerceptualBatch sharp {}; KirinPsbView psb {};
        if (spectrum) require (! post->pollSpectrumBatch (freq) || freq.latest.has_data == 0, "lost proof clears FREQ");
        else
        {
            require (! post->pollPerceptualBatch (sharp) || sharp.latest.has_data == 0, "lost proof clears SHARP");
            require (! post->pollPsb (psb) || psb.has_data == 0, "lost proof clears PSB");
        }
    }
    void verifyAvailability (bool spectrum)
    {
        // Each wrap gets its own deadline, rather than an aggregate success count that can
        // hide a whole missing lap. Recovery must finish within the existing 1.5 s request
        // lease: delay tail + calibration + exchange re-arm + complete analysis aperture.
        constexpr std::int64_t length = 96000, recovery = 72000;
        std::int64_t deadline = 0;
        int wraps = 0, recovered = 0, observed = 0;
        bool awaiting = false;
        const int blocks = preClock.loop ? 2 * static_cast<int> (length / frames) : 420;
        for (int i = 0; i < blocks; ++i)
        {
            const auto before = preClock.content;
            block();
            const bool hasData = zeroDelta (spectrum); // EVERY published delta must be zero.
            if (hasData) ++observed;
            if (! LiveTimingFixtureAccess::aaxPairedClock (*post).valid) noDelta (spectrum);
            if (preClock.loop && before / length != preClock.content / length)
            {
                require (! awaiting, "preceding loop must recover before another wrap");
                deadline = preClock.content + recovery;
                awaiting = true; ++wraps;
            }
            // A pre-wrap value does not count as recovery from the next callback's clamp.
            if (awaiting && before % length < recovery && hasData)
            { awaiting = false; ++recovered; }
            require (! awaiting || preClock.content <= deadline, "delta must recover within 1.5 s after each wrap");
        }
        // Complete the last wrap's deadline without skipping checks or accepting stale data.
        while (awaiting && preClock.content + frames <= deadline)
        {
            block();
            if (! LiveTimingFixtureAccess::aaxPairedClock (*post).valid) noDelta (spectrum);
            if (zeroDelta (spectrum)) { awaiting = false; ++recovered; }
        }
        require (preClock.loop || observed > 300, "linear data remains available");
        require (! awaiting && (! preClock.loop || (wraps >= 2 && recovered == wraps)), "every wrap recovered");
    }
    void cycleGuidance()
    {
        using namespace hypha::live_compare;
        require (post->analysisPairingLoopBlock() == LoopPairingBlock::auCycle
            && pre->analysisPairingLoopBlock() == LoopPairingBlock::none, "only unverified AU POST publishes Cycle guidance");
        require (! post->liveCompareStatus().preAudible, "unverified AU Cycle never auditions PRE");
    }
    void cycleListenRecovery (bool spectrum)
    {
        using namespace hypha::live_compare;
        require (post->startLiveCompare() == StartResult::started, "LISTEN starts while waiting for Cycle proof");
        post->selectLiveComparePre (true);
        for (int i = 0; i < 16; ++i)
        {
            block(); noDelta (spectrum); cycleGuidance();
            require (juce::String (hypha::live_compare_ui::namedRecovery (post->liveCompareStatus()))
                == "Turn Cycle off to compare (POST)", "LISTEN has the same callback's Cycle reason");
        }
        require (post->beginLiveBlind() == StartResult::started, "BLIND may prepare but not audition an unverified Cycle");
        for (int i = 0; i < 16; ++i)
        {
            block(); post->serviceLiveBlind(); noDelta (spectrum); cycleGuidance();
            const auto state = post->liveBlindStatus();
            require (state.stage == BlindStage::preparing && ! state.trial.active,
                     "initial Cycle wait never starts an anonymous trial");
            require (hypha::live_blind_ui::screenFor (state, true, 1.0f).instruction == "Turn Cycle off to compare",
                     "BLIND preparing has Cycle guidance");
        }
        post->finishLiveCompare();
        until ([&] { return ! post->liveCompareStatus().finishing; }, "END completes during the Cycle wait");
        preClock.loop = postClock.loop = false;
        until ([&] { return zeroDelta (spectrum) && ! post->analysisPairingLoopBlocked(); }, "Cycle off restores delta");
        require (post->startLiveCompare() == StartResult::started, "linear LISTEN starts again");
        post->selectLiveComparePre (true); auditionAllowed = true;
        until ([&] { return post->liveCompareStatus().preAudible; }, "linear proof restores PRE audition");
        require (post->beginLiveBlind() == StartResult::started, "BLIND can begin again after Cycle off");
        until ([&] { post->serviceLiveBlind(); return post->liveBlindStatus().stage == BlindStage::active; },
               "linear proof and MATCH restore BLIND");
        post->finishLiveCompare();
        until ([&] { return ! post->liveCompareStatus().finishing; }, "END returns after recovered BLIND");
        auditionAllowed = false;
    }
    int channels, delay = 0;
    bool reaper = false, auditionAllowed = false;
    double rate = 48000;
    Clock preClock, postClock;
    hypha::pdc_validation::FixedValidationDelay line;
    std::unique_ptr<Processor> pre, post;
    std::uint64_t owner = 0;
};
}

int main (int argc, char** argv)
try
{
    require (argc == 3 || argc == 4, "explicit layout, latency scenario and optional AU");
    const bool au = argc == 4 && juce::String (argv[3]) == "au";
    ReferenceValidationStorageSandbox sandbox;
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
    juce::ScopedJuceInitialiser_GUI initialise;
    const bool reaper = argc == 4 && juce::String (argv[3]) == "reaper";
    Fixture fixture (juce::String (argv[1]) == "mono" ? 1 : 2, argv[2], au, reaper);
    fixture.until ([&] { return fixture.post->setPairCandidate (fixture.pre->instanceId(), {}); }, "select PRE");
    for (bool loop : { false, true })
        for (bool spectrum : { true, false })
        {
            if (au && loop)
            {
                // Initial loops must establish independent occurrence proof. Do not retain a
                // previously linear K merely because the fixture ran a different mode first.
                fixture.postClock.available = false; fixture.block();
                fixture.postClock.available = true;
            }
            fixture.preClock.loop = fixture.postClock.loop = loop;
            hypha::analysis::Demand demand;
            demand.kind = spectrum ? hypha::analysis::Kind::spectrum : hypha::analysis::Kind::sharpDelta;
            require (fixture.post->setAnalysisDemand (fixture.owner, demand), "demand accepted");
            if (au && loop && juce::String (argv[2]) != "included")
            {
                int localObservations = 0;
                for (int i = 0; i < 900; ++i)
                {
                    fixture.block(); fixture.noDelta (spectrum); fixture.cycleGuidance();
                    KirinSpectrumBatch batch {};
                    if (spectrum && i >= 80 && fixture.post->pollSpectrumBatch (batch))
                    {
                        require (batch.latest.post_has_data != 0, "unverified paired AU Cycle keeps POST FREQ");
                        ++localObservations;
                    }
                }
                require (! spectrum || localObservations > 700, "POST FREQ is sustained during unverified Cycle");
                fixture.cycleListenRecovery (spectrum);
                continue;
            }
            fixture.until ([&] { return fixture.zeroDelta (spectrum); }, "aligned data after warmup");
            fixture.verifyAvailability (spectrum);
            if (au && ! loop)
            {
                fixture.preClock.projectWitness = false;
                for (int i = 0; i < 24; ++i) { fixture.block(); fixture.noDelta (spectrum); }
                fixture.preClock.projectWitness = true;
                fixture.until ([&] { return fixture.zeroDelta (spectrum); }, "independent project witness recovers");
            }
            fixture.postClock.available = false;
            if (reaper) fixture.postClock.positionAvailable = false;
            fixture.block(); fixture.noDelta (spectrum);
            fixture.postClock.available = fixture.postClock.positionAvailable = true;
            fixture.until ([&] { return fixture.zeroDelta (spectrum); }, "fresh data after proof recovery");
        }
    std::cout << "Content analysis product: physical delay, FREQ/SHARP/PSB, loop and recovery PASS\n";
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
