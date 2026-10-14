#pragma once
#include "LiveTimingFixtureAccess.h"
#include "ProductTestClock.h"
#include "TestProcessorStorage.h"
#include <stdexcept>

namespace hypha::tests
{
inline void requireInterruptedBlind (KirinHyphaProcessorBase& post,
                                     live_compare::RecoveryReason reason)
{
    const auto status = post.liveBlindStatus();
    // A revoked MATCH clears the public trial snapshot before service; teardown also ends
    // its command. The product stage, not that optional snapshot's bit, carries the failure.
    if (post.liveCompareStatus().preAudible || status.stage != live_compare::BlindStage::invalidated
        || status.reason != reason || post.selectLiveBlind (1) || post.selectLiveBlind (2)
        || post.revealLiveBlind())
        throw std::runtime_error ("missing PRE mapping ends old BLIND authority");
}

// Exercise the real callback -> public status -> service teardown boundary without an OS
// mapping. This is a status regression, not evidence for successful pair/MATCH admission.
inline void verifyBlindInterruptionStatus()
{
    using Processor = KirinHyphaProcessorBase;
    using namespace live_compare;
    struct Clock final : juce::AudioPlayHead
    {
        std::int64_t position = 0;
        bool positionAvailable = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (true);
            if (positionAvailable) info.setTimeInSamples (position);
            info.setKirinAuxiliaryClockSource (1);
            return info;
        }
    };
    for (int channels : { 1, 2 })
    {
        ProductTestClock wallClock;
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        Processor post (Processor::Role::Post);
        auto layout = post.getBusesLayout();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.inputBuses.set (0, set); layout.outputBuses.set (0, set);
        if (! post.setBusesLayout (layout)) throw std::runtime_error ("status fixture layout");
        Clock clock;
        post.setPlayHead (&clock);
        prepareFixtureProcessor (post, 96000, 2048);
        if (! LiveTimingFixtureAccess::configureVst3Clock (post, "REAPER", "7.71"))
            throw std::runtime_error ("status fixture clock certificate");
        juce::AudioBuffer<float> buffer (channels, 2048);
        juce::MidiBuffer midi;
        const auto block = [&] {
            for (int c = 0; c < channels; ++c)
                for (int f = 0; f < 2048; ++f) buffer.setSample (c, f, 0.125f);
            post.processBlock (buffer, midi);
            for (int c = 0; c < channels; ++c)
                for (int f = 0; f < 2048; ++f)
                    if (buffer.getSample (c, f) != 0.125f)
                        throw std::runtime_error ("missing-proof callback preserves every POST sample");
            if (post.getLatencySamples() != 0) throw std::runtime_error ("status fixture zero latency");
            clock.position += 2048;
            wallClock.advanceBlock (2048, 96000);
        };
        block(); // establish the processing clock before seeding the active status
        LiveTimingFixtureAccess::seedBlindStatusFixture (post);
        const auto active = post.liveBlindStatus();
        if (active.stage != BlindStage::active || ! active.trial.active || active.trial.invalidated)
            throw std::runtime_error ("status fixture begins with a current active trial");
        clock.positionAvailable = false;
        block();
        const auto terminal = LiveTimingFixtureAccess::blindTrial (post);
        if (! terminal.invalidated || terminal.reason != RecoveryReason::preUnavailable)
            throw std::runtime_error ("missing owner invalidates the exact trial before service");
        requireInterruptedBlind (post, RecoveryReason::preUnavailable);
        post.serviceLiveBlind();
        requireInterruptedBlind (post, RecoveryReason::preUnavailable);
        clock.positionAvailable = true;
        for (int i = 0; i < 40; ++i)
        {
            block(); post.serviceLiveBlind();
            requireInterruptedBlind (post, RecoveryReason::preUnavailable);
        }
        post.releaseResources();
    }
}
}
