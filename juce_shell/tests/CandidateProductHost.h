#pragma once
#include "ProductTestClock.h"
#include "LiveTimingFixtureAccess.h"
#include "TestProcessorStorage.h"
#include "AnalysisWorkerReceipt.h"
#include "CandidateProductUi.h"
#include "CandidateAllocationProbe.h"
#include "CandidateOutputOracle.h"
#include "CandidateTraceTest.h"
#include "../src/AtomicFile.h"
#include "ValidationStorageSandbox.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <optional>
#include <thread>

namespace hypha::tests
{
template<class Require> void writeCandidateFixtureIdentity (Require require, const juce::File& storage)
{
   #if JUCE_WINDOWS
    const auto identity = storage.getChildFile ("home/AppData/Roaming/Kirin OS/identity.json");
   #else
    const auto identity = storage.getChildFile ("home/Library/Application Support/Kirin OS/identity.json");
   #endif
    require (identity.isAChildOf (storage) && identity.getParentDirectory().createDirectory().wasOk()
        && hypha::storage::writeText (identity, R"({"schema_version":"1.0","installation_id":"fixture","hardware_id":"fixture","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"fixture","license":"os","created_at":"2026-01-01T00:00:00Z","last_verified_at":"2026-01-01T00:00:00Z"})"),
        "fixture-only identity enables real Record admission");
}

struct CandidateClock final : juce::AudioPlayHead
{
    std::int64_t clock = 0, delay = 0, offset = 0, loopLength = 0, continuousOffset = 0;
    bool playing = true, available = true;
    int source = 1, reportFrames = 0;
    double sampleRate = 48000.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        const auto reported = reportFrames > 0 ? clock - clock % reportFrames : clock;
        auto project = reported - delay + offset;
        if (loopLength > 0)
        {
            project = (project % loopLength + loopLength) % loopLength;
            info.setIsLooping (true); info.setBpm (120.0);
            info.setPpqPosition (static_cast<double> (project) / (sampleRate * 0.5));
            info.setLoopPoints (LoopPoints { 0.0, static_cast<double> (loopLength) / (sampleRate * 0.5) });
        }
        if (available) { info.setTimeInSamples (project); info.setTimeInSeconds (static_cast<double> (project) / sampleRate); }
        info.setKirinAuxiliaryClockSource (source);
        info.setKirinAuxiliaryClockSamples (clock + continuousOffset);
        info.setKirinPresentationLatencySource (1);
        info.setKirinOutputPresentationLatencySamples (0);
        return info;
    }
};
inline bool candidateBroadband = false; // noise has one position; the sines below repeat
inline float candidateInput (std::int64_t frame, int channel)
{
    const auto at = static_cast<double> (frame);
    if (candidateBroadband && frame >= 0)
    {
        auto z = static_cast<std::uint64_t> (frame) * 0x9E3779B97F4A7C15ull + static_cast<std::uint64_t> (channel) * 0xD1B54A32D192ED03ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return 0.13f * (static_cast<float> ((z ^ (z >> 31)) >> 40) / 8388608.0f - 1.0f);
    }
    return frame < 0 ? 0.0f : static_cast<float> (0.08 * std::sin (at * 0.023 + channel)
        + 0.05 * std::sin (at * 0.071 + channel * 0.3));
}
inline float candidateA (std::int64_t frame, int channel, float scale = 1.0f)
{
    return std::tanh (candidateInput (frame, channel) * scale * 3.0f) * 0.7f
        + std::tanh (candidateInput (frame - 211, channel) * scale * 2.0f) * 0.1f;
}
inline float candidateB (float input, float echo)
{ return std::tanh (input * 5.0f) * 0.4f + std::tanh (echo * 4.0f) * 0.08f; }
inline bool candidateIdentical (float a, float b) { return std::memcmp (&a, &b, sizeof (a)) == 0; }

// Synthetic serial host, using the real JUCE/Rust entry points. Indexed input and independent
// effect formulas are the oracle: no expected PCM comes from the ring or its correspondence.
template<class Require> class CandidateHost
{
public:
    using P = KirinHyphaProcessorBase;
    const int block;
    ProductTestClock& wall;
    Require require;
    bool aax = false;
    int channels, delayA = 127, delayB = 383;
    int reportedDelayA = 0, reportedDelayB = 0; // added to physical delays only in host positions
    double sampleRate = 48000.0;
    bool postEnabled = true, pre1Enabled = true, pre2Enabled = true, auditTransition = false, overlapLease = false, heartbeatAhead = false;
    float inputScale = 1.0f, lastWeight = 0.0f;
    std::array<CandidateClock, 3> clocks;
    std::array<std::unique_ptr<P>, 3> instances;
    std::vector<P*> passengers; // other pairs of the project, fed after POST
    juce::AudioBuffer<float> audio;
    juce::MidiBuffer midi;
    std::array<std::vector<float>, 2> bHistory;
    std::int64_t cursor = 0;
    int replacementBlocks = 0, matchedBlocks = 0;
    bool wholeOutput = false, serviceBlindAfterAudio = true, processorTimers = false;
    std::optional<bool> initialBlindSourceA;
    int blindStarts = 0;
    int timerBlocks = 1, timerCounter = 0; // real timers fire tens of milliseconds apart
    CandidateContinuityOracle sourceContinuity, outputContinuity;
    CandidateOutputOracle outputOracle;
    CandidateReceiverOracle receiverOracle;

    CandidateHost (ProductTestClock& time, Require check, int channelCount, int frames, bool aaxClock = false)
        : block (frames), wall (time), require (check), aax (aaxClock), channels (channelCount), audio (channels, block)
    {
        // Different continuous origins make POST's K negative in the large-block loop case.
        if (block == 1024) clocks[1].continuousOffset = 4096;
        for (std::size_t n = 0; n < instances.size(); ++n)
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (aax ? juce::AudioProcessor::wrapperType_AAX : juce::AudioProcessor::wrapperType_VST3);
            instances[n] = std::make_unique<P> (n == 2 ? P::Role::Post : P::Role::Pre);
            auto layout = instances[n]->getBusesLayout();
            layout.inputBuses.set (0, channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
            layout.outputBuses.set (0, layout.inputBuses[0]);
            require (instances[n]->setBusesLayout (layout), "A/B negotiates mono and stereo");
            instances[n]->setPlayHead (&clocks[n]);
            juce::AudioProcessor::TrackProperties properties;
            properties.name = n == 1 ? "Other track" : "Serial track";
            instances[n]->updateTrackProperties (properties);
            prepareFixtureProcessor (*instances[n], 48000, block);
            LiveTimingFixtureAccess::enableFixtureWrites (*instances[n]);
            if (aax) { clocks[n].source = 3; LiveTimingFixtureAccess::configureAaxClock (*instances[n], true); }
            require (instances[n]->getLatencySamples() == 0, "A/B adds no reported latency");
        }
        for (auto& ch : bHistory) ch.resize (48000 * 320);
        // This periodic fixture isolates A/B audio and timing, not content alignment.
        instances[2]->setLiveCompareAutoAlign (false);
    }
    ~CandidateHost() { for (auto& instance : instances) if (instance) instance->releaseResources(); }
    P& pre1() { return *instances[0]; }
    P& pre2() { return *instances[1]; }
    P& post() { return *instances[2]; }
    void changeFormat (P& processor)
    {
        processor.releaseResources();
        prepareFixtureProcessor (processor, 44100, block);
        prepareFixtureProcessor (processor, 48000, block);
        LiveTimingFixtureAccess::enableFixtureWrites (processor);
        if (aax) LiveTimingFixtureAccess::configureAaxClock (processor, true);
    }
    void setSampleRate (double rate)
    {
        sampleRate = rate;
        for (std::size_t n = 0; n < instances.size(); ++n)
        {
            clocks[n].sampleRate = rate;
            if (!instances[n]) continue;
            instances[n]->releaseResources();
            prepareFixtureProcessor (*instances[n], rate, block);
            LiveTimingFixtureAccess::enableFixtureWrites (*instances[n]);
            if (aax) LiveTimingFixtureAccess::configureAaxClock (*instances[n], true);
        }
        for (auto* passenger : passengers)
        {
            passenger->releaseResources();
            prepareFixtureProcessor (*passenger, rate, block);
            LiveTimingFixtureAccess::enableFixtureWrites (*passenger);
        }
    }
    float history (int channel, std::int64_t at) const
    { return at >= 0 ? bHistory[static_cast<std::size_t> (channel)][static_cast<std::size_t> (at)] : 0.0f; }
    void callback (P& processor)
    {
        { const candidate_allocations::Callback probe; processor.processBlock (audio, midi); }
        require (candidate_allocations::allocations == 0 && candidate_allocations::deletions == 0,
                 "actual candidate callbacks never allocate or destroy C++ storage");
    }
    void render (int count, bool comparing, bool selectA, bool oracle = true)
    {
        using namespace live_compare;
        for (int step = 0; step < count; ++step, cursor += block)
        {
            wall.advanceBlock (block, sampleRate);
            clocks[1].delay = delayA + reportedDelayA;
            clocks[2].delay = delayA + delayB + reportedDelayA + reportedDelayB;
            for (auto& clock : clocks) clock.clock = cursor;
            auto* changing = instances[1] ? const_cast<SharedRingMapping*> (LiveTimingFixtureAccess::publishedRing (pre2())) : nullptr;
            auto* request = changing && changing->ring() ? &changing->ring()->header.candidate : nullptr;
            if (request && heartbeatAhead) request->refreshLease (request->owner.load(), wall.nanos() + 1000000);
            if (request && overlapLease) request->leaseSequence.fetch_add (1);
            for (auto& instance : instances) if (instance) instance->serviceLiveCompare();
            if (processorTimers && timerCounter++ % timerBlocks == 0)
                for (auto& instance : instances) if (instance) LiveTimingFixtureAccess::serviceProcessorTimer (*instance);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < block; ++i) audio.setSample (ch, i, candidateInput (cursor + i, ch) * inputScale);
            if (pre1Enabled && instances[0]) callback (pre1());
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    require (candidateIdentical (audio.getSample (ch, i), candidateInput (cursor + i, ch) * inputScale), "PRE1 bit identity");
                    audio.setSample (ch, i, candidateA (cursor + i - delayA, ch, inputScale));
                }
            if (pre2Enabled && instances[1]) callback (pre2());
            const auto* mapping = instances[1] ? LiveTimingFixtureAccess::publishedRing (pre2()) : nullptr;
            const auto* receipt = mapping && mapping->ring() ? &mapping->ring()->header.candidate : nullptr;
            const auto receiptClock = cursor + clocks[1].continuousOffset;
            const bool exact = comparing && receipt && receipt->covers (receipt->owner.load(),
                mapping->ring()->header.run.load(), receiptClock, receiptClock + block);
            sourceContinuity.check (require, audio,
                [&] (int i, int ch) { return candidateInput (cursor + i - delayA, ch) * inputScale; },
                "continuous PRE2 input never falls back after replacement begins");
            if (exact) ++replacementBlocks;
            if (wholeOutput) receiverOracle.check (require, audio, exact,
                [&] (int i, int ch) { return candidateA (cursor + i - delayA, ch, inputScale); });
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const auto value = audio.getSample (ch, i);
                    if (auditTransition && ch == 0)
                    {
                        const auto x = candidateInput (cursor + i - delayA, ch) * inputScale;
                        const auto a = candidateA (cursor + i - delayA, ch, inputScale);
                        if (std::abs (x - a) > 0.001f)
                        {
                            const auto weight = (value - a) / (x - a);
                            require (weight >= -0.0001f && weight <= 1.0001f
                                && std::abs (weight - lastWeight) <= 0.02f, "START / END crossfade has no step");
                            lastWeight = weight;
                        }
                    }
                    if (exact) require (std::abs (value - candidateInput (cursor + i - delayA, ch) * inputScale) < 1.0e-7f,
                                        "PRE2 sends aligned unity x after publishing a");
                    if (! comparing)
                    {
                        const bool identical = candidateIdentical (value, candidateA (cursor + i - delayA, ch, inputScale));
                        if (!identical) std::cerr << "PRE2 pass-through: cursor=" << cursor << " step=" << step
                            << " frame=" << i << " active=" << (instances[2] && post().candidateCompareActive())
                            << " returning=" << (instances[2] && post().candidateReturning())
                            << " postEnabled=" << postEnabled << " offline=" << pre2().isNonRealtime()
                            << " bypass=" << pre2().getBypassParameter()->getValue() << '\n';
                        require (identical, "unselected PRE2 bit identity");
                    }
                    bHistory[static_cast<std::size_t> (ch)][static_cast<std::size_t> (cursor + i)] = value;
                    const auto at = cursor + i - delayB;
                    audio.setSample (ch, i, candidateB (history (ch, at), history (ch, at - 997)));
                }
            if (request && overlapLease) request->leaseSequence.fetch_add (1);
            if (! postEnabled || ! instances[2]) continue;
            const auto blindCommand = LiveTimingFixtureAccess::command (post());
            const bool sourceA = blindCommand.active() ? blindCommand.pre() : selectA;
            callback (post());
            if (post().candidateMeasurementIsolated())
                require (post().signalStateLive() == KIRIN_SIGNAL_STATE_INACTIVE,
                         "comparison input never activates canonical Watch measurement");
            const auto status = post().liveCompareStatus();
            outputContinuity.check (require, audio, [&] (int i, int ch)
            {
                const auto at = cursor + i - delayA - delayB;
                return sourceA ? candidateA (at, ch, inputScale) * status.gain
                    : candidateB (candidateInput (at, ch) * inputScale,
                                  candidateInput (at - 997, ch) * inputScale) * status.postActual;
            }, "continuous LISTEN / BLIND never inserts a fallback or the serial effect chain");
            if (wholeOutput && status.verdict == Verdict::accepted && (!sourceA || status.preAudible)) ++matchedBlocks;
            if ((wholeOutput || (comparing && oracle)) && status.verdict == Verdict::accepted)
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < block; ++i)
                        require (std::abs (history (ch, cursor + i - delayB)
                            - candidateInput (cursor + i - delayA - delayB, ch) * inputScale) < 1.0e-7f,
                                 "accepted B input is independently aligned unity x, even when B retains an earlier tail");
            if (wholeOutput)
                outputOracle.check (require, audio, status.verdict == Verdict::accepted, sourceA,
                    [&] (int i, int ch) { return candidateA (cursor + i - delayA - delayB, ch, inputScale) * status.gain; },
                    [&] (int i, int ch) { const auto at = cursor + i - delayB;
                        return candidateB (history (ch, at), history (ch, at - 997)) * status.postActual; });
            if (! wholeOutput && oracle && comparing && step > 30 && status.verdict == Verdict::accepted && (!sourceA || status.preAudible))
            {
                ++matchedBlocks;
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < block; ++i)
                    {
                        const auto at = cursor + i - delayA - delayB;
                        // The current B input must be independent unity x (checked above),
                        // while its echo can still contain an earlier unverified input.
                        const auto expected = sourceA ? candidateA (at, ch, inputScale) * status.gain
                            : candidateB (candidateInput (at, ch) * inputScale,
                                history (ch, cursor + i - delayB - 997)) * status.postActual;
                        if (std::abs (audio.getSample (ch, i) - expected) >= 3.0e-6f)
                            std::cerr << "candidate sample " << cursor + i << ", channel " << ch
                                << ", delay " << delayA << "/" << delayB << ", source " << (sourceA ? "A" : "B") << ": " << audio.getSample (ch, i)
                                << " expected " << expected << " align=" << post().liveCompareAlignFrames() << '\n';
                        require (std::abs (audio.getSample (ch, i) - expected) < 3.0e-6f,
                                 "A and B match independent nonlinear, tail and delay oracles, never B(A(x))");
                    }
            }
            for (auto* passenger : passengers) { passenger->serviceLiveCompare(); callback (*passenger); }
            // Service may assign SOURCE 1 for the NEXT callback. Finish this block's PCM
            // oracle first, using only the command that its real audio entry observed.
            if (serviceBlindAfterAudio)
            {
                post().serviceLiveBlind();
                if (!blindCommand.active() && LiveTimingFixtureAccess::command (post()).active())
                {
                    ++blindStarts;
                    if (initialBlindSourceA)
                        LiveTimingFixtureAccess::assignBlindForTest (post(), *initialBlindSourceA);
                }
            }
        }
    }
    // Keep needs PRE's announcement, which its IO worker writes on its own wall-clock schedule.
    // A refusal that outlasts this bound is a real one.
    void requireKeep (P& processor, const char* message)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (10);
        bool kept = processor.keepPair();
        while (!kept && std::chrono::steady_clock::now() < deadline)
        {
            render (1, false, false);
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            kept = processor.keepPair();
        }
        if (!kept) std::cerr << "Keep refused: \"" << processor.drainKeepActionNotice() << "\" phase=" << processor.keepPhase()
            << " recording=" << processor.isRecording() << " refused="
            << processor.outputDecision (output_owner::Activity::recordKeep).refused() << '\n';
        require (kept, message);
    }
    void pair()
    {
        render (60, false, false);
        for (auto& p : instances) { awaitAnalysisInput (*p); awaitAnalysisExchange (*p, true); }
        require (post().setPairCandidate (pre1().instanceId(), "Source"), "normal pair through product API");
    }
};

}
