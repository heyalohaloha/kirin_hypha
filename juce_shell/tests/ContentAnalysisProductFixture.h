#pragma once
#include "../src/PluginProcessor.h"
#include "AnalysisProductTiming.h"
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

namespace content_analysis_product
{
inline void require (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); }
using Processor = KirinHyphaProcessorBase;
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
            const auto length = loopSamples;
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
    std::int64_t loopSamples = 96000;
    double rate = 48000;
    bool reaper = false, positionAvailable = true;
    bool audioUnit = false, projectWitness = true;
    bool reported = true, available = true, playing = true, loop = false;
};

struct Fixture
{
    Fixture (int channelsIn, const juce::String& scenario, bool au, bool reaperIn,
             hypha::tests::AnalysisTestClock::time_point end)
        : durations (reaperIn), frames (hypha::tests::analysisProductFrames (reaperIn)),
          channels (channelsIn), reaper (reaperIn), phaseDeadline (end)
    {
        preClock.audioUnit = postClock.audioUnit = au;
        preClock.reaper = postClock.reaper = reaper;
        preClock.rate = postClock.rate = rate = static_cast<double> (durations.rate);
        preClock.loopSamples = postClock.loopSamples = static_cast<std::int64_t> (durations.samples (durations.loopMs));
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
        juce::MidiBuffer midi;
        KirinSpectrumStats before {}, after {};
        const bool haveStats = reaper && LiveTimingFixtureAccess::analysisStats (processor, before);
        (&processor == pre.get() ? preIntervals : postIntervals).observe (
            hypha::tests::AnalysisTestClock::now(), frames, rate);
        processor.processBlock (audio, midi);
        if (haveStats && LiveTimingFixtureAccess::analysisStats (processor, after))
        {
            const auto dropped = after.dropped_blocks - before.dropped_blocks;
            const bool valid = LiveTimingFixtureAccess::aaxLocalClock (processor).valid;
            auto& counts = &processor == pre.get() ? preDrops : postDrops;
            (valid ? counts.withClock : counts.withoutClock) += dropped;
            // Clock retirement contributes to dropped_blocks too. Only loss with a valid
            // callback is unexplained here (queue pressure or ingress rejection); never retry it.
            require (! valid || dropped == 0, "fixture lost analysis input with a valid clock");
        }
        for (int ch = 0; ch < channels; ++ch)
            if (auditionAllowed && &processor == post.get())
            {
                for (int i = 0; i < frames; ++i)
                    require (std::abs (audio.getSample (ch, i) - original.getSample (ch, i)) < 1.0e-6f,
                             "verified PRE audition matches the physical-delay input, including its fade");
            }
            else require (std::memcmp (audio.getReadPointer (ch), original.getReadPointer (ch),
                                      static_cast<std::size_t> (frames) * sizeof (float)) == 0, "normal A must remain bit identical");
        require (processor.getLatencySamples() == 0, "zero latency");
    }
    void block()
    {
        require (hypha::tests::AnalysisTestClock::now() < phaseDeadline, "phase wall watchdog before callback");
        const auto started = hypha::tests::AnalysisTestClock::now();
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
        injectedDelay.beforePost (callbacks);
        process (*post, audio);
        preClock.content += frames; postClock.content += frames;
        LiveTimingFixtureAccess::serviceTiming (*post);
        ++callbacks;
        // Worker/IO scheduling is real, but input progress is counted independently below.
        // Do not add a fixed sleep to a slow callback, or catch up with a burst of queued audio.
        hypha::tests::finishAnalysisProductBlock (started, reaper);
        require (hypha::tests::AnalysisTestClock::now() < phaseDeadline, "phase wall watchdog after callback");
    }
    template <class Check> void until (Check check, const char* why)
    {
        const auto started = hypha::tests::AnalysisTestClock::now();
        const auto first = callbacks;
        std::cerr << "[block " << first << "] " << why << std::endl;
        const auto result = hypha::tests::waitForAnalysis (
            hypha::tests::analysisWaitBlocks (durations.rate, static_cast<std::uint64_t> (frames)), phaseDeadline,
            [&] { block(); }, check, [] { return hypha::tests::AnalysisTestClock::now(); });
        const auto elapsed = std::chrono::duration<double> (hypha::tests::AnalysisTestClock::now() - started).count();
        std::cerr << "  " << callbacks - first << " blocks, " << elapsed << " wall seconds" << std::endl;
        if (result == hypha::tests::AnalysisWaitResult::ready) return;
        std::cerr << (result == hypha::tests::AnalysisWaitResult::audioLimit ? "audio limit" : "wall watchdog") << std::endl;
        require (false, why);
    }
    void diagnostics()
    {
        std::cerr << "frames " << frames << ", rate " << rate << ", callbacks " << callbacks
                  << ", injected delays " << injectedDelay.injections << std::endl;
        preIntervals.print (std::cerr, "PRE"); postIntervals.print (std::cerr, "POST");
        for (auto* processor : { pre.get(), post.get() })
        {
            KirinSpectrumStats stats {};
            if (LiveTimingFixtureAccess::analysisStats (*processor, stats))
            {
                const auto& drops = processor == pre.get() ? preDrops : postDrops;
                std::cerr << (processor == pre.get() ? "PRE" : "POST") << " worker: enabled "
                          << static_cast<int> (stats.enabled) << ", pushed " << stats.pushed_blocks
                          << ", dropped " << stats.dropped_blocks << ", drops without clock "
                          << drops.withoutClock << ", drops with clock " << drops.withClock << std::endl;
            }
        }
        std::cerr << std::flush;
    }

    bool zeroDelta (bool spectrum)
    {
        latestEndpoint = 0; latestAperture = 0;
        if (spectrum)
        {
            KirinSpectrumBatch batch {};
            if (! post->pollSpectrumBatch (batch) || batch.latest.has_data == 0) return false;
            for (auto value : batch.latest.display_db) require (std::abs (value) < 1.0e-5f, "same-content FREQ delta");
            latestEndpoint = batch.latest.presentation_end_samples;
            latestAperture = batch.latest.aperture_samples;
            return true;
        }
        KirinPerceptualBatch batch {}; KirinPsbView psb {};
        if (! post->pollPerceptualBatch (batch) || batch.latest.has_data == 0
            || ! post->pollPsb (psb) || psb.has_data == 0) return false;
        require (std::abs (batch.latest.delta_sharpness) < 1.0e-8, "same-content SHARP delta");
        require (psb.is_delta != 0, "PSB is delta");
        for (auto value : psb.shares) require (std::abs (value) < 1.0e-8, "same-content PSB delta");
        latestEndpoint = batch.latest.presentation_end_samples;
        latestAperture = batch.latest.aperture_samples;
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
        // hide a whole missing lap. The SHARP budget includes its future epoch and two
        // continuous 100 ms apertures at 96 kHz; see AnalysisAudioDurations::recoverySamples.
        const auto length = static_cast<std::int64_t> (durations.samples (durations.loopMs));
        const auto recovery = durations.recoverySamples (spectrum, static_cast<std::uint64_t> (frames),
                                                         static_cast<std::uint64_t> (delay));
        require (recovery < static_cast<std::uint64_t> (length), "loop must outlast its recovery budget");
        std::int64_t deadline = 0;
        int wraps = 0, recovered = 0;
        std::uint64_t observedSamples = 0;
        bool awaiting = false;
        hypha::tests::AnalysisWrapRecovery fresh;
        const auto recoveredNow = [&] (bool hasData) {
            if (! reaper) return hasData;
            return fresh.observe (LiveTimingFixtureAccess::aaxPairedClock (*post).valid, hasData,
                latestEndpoint, latestAperture, postClock.content % length);
        };
        std::cerr << "loop samples " << length << ", recovery samples " << recovery << std::endl;
        const auto samples = durations.samples (preClock.loop ? 2 * durations.loopMs : durations.linearMs);
        const auto blocks = hypha::tests::analysisBlocksForSamples (samples, static_cast<std::uint64_t> (frames));
        for (std::uint64_t i = 0; i < blocks; ++i)
        {
            const auto before = preClock.content;
            block();
            const bool hasData = zeroDelta (spectrum); // EVERY published delta must be zero.
            if (hasData) observedSamples += std::min (static_cast<std::uint64_t> (frames),
                                                      samples - i * static_cast<std::uint64_t> (frames));
            if (! LiveTimingFixtureAccess::aaxPairedClock (*post).valid) noDelta (spectrum);
            if (preClock.loop && before / length != preClock.content / length)
            {
                require (! awaiting, "preceding loop must recover before another wrap");
                // Anchor at the actual wrap, not the end of a larger block crossing it.
                deadline = durations.recoveryDeadlineAfterWrap (before, recovery);
                fresh = {};
                awaiting = true; ++wraps;
                std::cerr << "wrap " << wraps << " at " << deadline - static_cast<std::int64_t> (recovery)
                          << ", deadline " << deadline << std::endl;
            }
            // A pre-wrap value does not count as recovery from the next callback's clamp.
            if (awaiting && before % length < static_cast<std::int64_t> (recovery)
                && recoveredNow (hasData) && preClock.content <= deadline)
            {
                awaiting = false; ++recovered;
                std::cerr << "wrap " << wraps << " recovered at " << preClock.content
                          << ", deadline " << deadline << ", endpoint " << latestEndpoint << std::endl;
            }
            require (! awaiting || preClock.content <= deadline, "delta must recover within the per-wrap audio deadline");
        }
        // Complete the last wrap's deadline without skipping checks or accepting stale data.
        while (awaiting && preClock.content + frames <= deadline)
        {
            block();
            if (! LiveTimingFixtureAccess::aaxPairedClock (*post).valid) noDelta (spectrum);
            if (recoveredNow (zeroDelta (spectrum)))
            {
                awaiting = false; ++recovered;
                std::cerr << "wrap " << wraps << " recovered at " << preClock.content
                          << ", deadline " << deadline << ", endpoint " << latestEndpoint << std::endl;
            }
        }
        require (preClock.loop || observedSamples > durations.samples (durations.minimumDataMs), "linear data remains available");
        require (! awaiting && (! preClock.loop || (wraps >= 2 && recovered == wraps)), "every wrap recovered");
    }
    void cycleGuidance()
    {
        using namespace hypha::live_compare;
        require (post->analysisPairingLoopBlock() == LoopPairingBlock::auCycle
            && pre->analysisPairingLoopBlock() == LoopPairingBlock::none, "only unverified AU POST publishes Cycle guidance");
        require (! post->liveCompareStatus().preAudible, "unverified AU Cycle never auditions PRE");
    }
    const hypha::tests::AnalysisAudioDurations durations;
    const int frames;
    int channels, delay = 0;
    hypha::tests::AnalysisPostDelay injectedDelay;
    bool reaper = false, auditionAllowed = false;
    const hypha::tests::AnalysisTestClock::time_point phaseDeadline;
    hypha::tests::AnalysisCallbackIntervals preIntervals, postIntervals;
    struct Drops { std::uint64_t withoutClock = 0, withClock = 0; } preDrops, postDrops;
    std::int64_t latestEndpoint = 0;
    std::uint64_t latestAperture = 0;
    double rate = 48000;
    Clock preClock, postClock;
    hypha::pdc_validation::FixedValidationDelay line;
    std::unique_ptr<Processor> pre, post;
    std::uint64_t owner = 0, callbacks = 0;
};
}
