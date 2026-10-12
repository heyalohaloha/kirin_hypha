#include "../src/PluginProcessor.h"
#include "ReferenceValidationStorageSandbox.h"
#include "LiveTimingFixtureAccess.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using Processor = KirinHyphaProcessorBase;
using namespace hypha::live_compare;
constexpr int frames = 256;
void require (bool ok, const char* message)
{
    if (! ok) throw std::runtime_error (message);
}
struct Clock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setTimeInSamples (position);
        info.setKirinAuxiliaryClockSource (source);
        info.setKirinAuxiliaryClockSamples (position + 960000);
        return info;
    }
    std::int64_t position = 0;
    int source = 2;
};
std::unique_ptr<Processor> prepare (Processor::Role role, juce::AudioProcessor::WrapperType format,
                                    int channels, Clock& clock)
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (format);
    auto processor = std::make_unique<Processor> (role);
    auto layout = processor->getBusesLayout();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.set (0, set); layout.outputBuses.set (0, set);
    require (processor->setBusesLayout (layout), "mono/stereo negotiates");
    if (format == juce::AudioProcessor::wrapperType_AAX)
        processor->kirinHostInstanceGroup (role == Processor::Role::Pre ? 101 : 102, true);
    processor->setPlayHead (&clock);
    processor->prepareToPlay (48000, frames);
    require (! LiveTimingFixtureAccess::timingRingPublished (*processor), "no ring before identity/writes are enabled");
    LiveTimingFixtureAccess::enableFixtureWrites (*processor);
    require (processor->instanceId().isNotEmpty() && processor->stereoWorkflowsSupported(),
             "PRE publication prerequisites are established by actual prepare/enable");
    return processor;
}
void normalBlock (Processor& processor, int channels)
{
    juce::AudioBuffer<float> buffer (channels, frames), original (channels, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i) buffer.setSample (c, i, static_cast<float> (i - 128) / 1024.0f);
    original.makeCopyOf (buffer);
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    for (int c = 0; c < channels; ++c)
        require (std::memcmp (buffer.getReadPointer (c), original.getReadPointer (c), frames * sizeof (float)) == 0,
                 "normal A stays bit identical");
    require (processor.getLatencySamples() == 0, "normal A latency stays zero");
}
void selectPair (Processor& pre, Processor& post, Clock& clock, int channels)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
    while (! post.setPairCandidate (pre.instanceId(), {}))
    {
        require (std::chrono::steady_clock::now() < deadline, "real PRE is discovered for exact pairing");
        normalBlock (pre, channels); normalBlock (post, channels); clock.position += frames;
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
    }
    require (post.pairedPreInstanceId() == pre.instanceId(), "POST opens the actual PRE identity");
}
void stateOnly (juce::AudioProcessor::WrapperType format, int channels)
{
    Clock clock;
    if (format == juce::AudioProcessor::wrapperType_AAX) clock.source = 3;
    auto pre = prepare (Processor::Role::Pre, format, channels, clock);
    auto post = prepare (Processor::Role::Post, format, channels, clock);
    require (LiveTimingFixtureAccess::publishUnmappedRing (*pre), "publish an unmapped lease control");
    for (int reset = 0; reset < 4; ++reset)
    {
        if (reset != 0) pre->kirinHostTimelineReset (reset != 3, reset == 2);
        LiveTimingFixtureAccess::serviceProcessorTimer (*pre);
        require (LiveTimingFixtureAccess::timingRingPublished (*pre), "PRE timer must retain its publishing lifetime");
        require (pre->startLiveCompare() == StartResult::notPost
            && LiveTimingFixtureAccess::timingRingPublished (*pre), "PRE cannot start LISTEN or lose its ring through POST service");
        normalBlock (*pre, channels); clock.position += frames;
    }
    juce::MemoryBlock saved;
    pre->getStateInformation (saved);
    pre->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    LiveTimingFixtureAccess::serviceProcessorTimer (*pre);
    require (LiveTimingFixtureAccess::timingRingPublished (*pre)
        && ! LiveTimingFixtureAccess::serviceTimerRunning (*pre), "PRE restore retains publication and acknowledges service without idle polling");
    require (post->startLiveCompare() == StartResult::noPair, "POST without pair stops before open");
    selectPair (*pre, *post, clock, channels);
    // Remove any real mapping the environment may have created; the positive start belongs only
    // to sharedRing(). Here preUnavailable proves POST passed identity/layout/renderer admission.
    LiveTimingFixtureAccess::retirePreparedFormatRing (*pre);
    require (post->startLiveCompare() == StartResult::preUnavailable
        && ! post->liveCompareStatus().active, "missing PRE fails explicitly after POST reaches open");
    require (post->beginLiveBlind() == StartResult::preUnavailable
        && ! post->liveCompareStatus().active, "missing PRE also refuses direct BLIND before starting its trial");
    require (LiveTimingFixtureAccess::publishUnmappedRing (*post), "publish a POST retirement control");
    LiveTimingFixtureAccess::fadePending (*post, true);
    post->serviceLiveCompare();
    require (LiveTimingFixtureAccess::timingRingPublished (*post)
        && post->startLiveCompare() == StartResult::notReady, "POST pending fade retains lease and prevents renderer prepare/open");
    LiveTimingFixtureAccess::fadePending (*post, false);
    post->serviceLiveCompare();
    require (! LiveTimingFixtureAccess::timingRingPublished (*post), "POST completed fade retires its lease");
    require (LiveTimingFixtureAccess::publishUnmappedRing (*post), "publish an END receipt control");
    post->finishLiveCompare(); post->serviceLiveCompare();
    require (LiveTimingFixtureAccess::timingRingPublished (*post), "pending END receipt retains POST lease");
    pre->releaseResources(); post->releaseResources();
}
void sharedRing (juce::AudioProcessor::WrapperType format, int channels)
{
    Clock clock;
    if (format == juce::AudioProcessor::wrapperType_AAX) clock.source = 3;
    auto pre = prepare (Processor::Role::Pre, format, channels, clock);
    require (LiveTimingFixtureAccess::timingRingPublished (*pre), "actual PRE prepare/enable must create OS shared memory");
    const auto* owner = LiveTimingFixtureAccess::publishedRing (*pre);
    const auto key = pairKeyForPreInstance (pre->instanceId().toStdString());
    require (owner != nullptr && owner->ring() != nullptr && owner->ring()->matches (key, 48000),
             "PRE key/rate/version matches its identity");
    const auto a = owner->ring()->header.timing.ownerA.load(), b = owner->ring()->header.timing.ownerB.load();
    auto post = prepare (Processor::Role::Post, format, channels, clock);
    for (int tick = 0; tick < 4; ++tick) LiveTimingFixtureAccess::serviceProcessorTimer (*pre);
    require (LiveTimingFixtureAccess::timingRingPublished (*pre)
        && owner->ring()->header.ownerClosed.load() == 0, "PRE owner survives idle service after publication");
    selectPair (*pre, *post, clock, channels);
    require (post->startLiveCompare() == StartResult::started && post->liveCompareStatus().active,
             "LISTEN starts through real PRE publication and POST open");
    require (owner->ring()->header.demand.load() == 1, "LISTEN requests PRE PCM");
    post->stopLiveCompare(); post->serviceLiveCompare();
    require (! LiveTimingFixtureAccess::timingRingPublished (*post)
        && LiveTimingFixtureAccess::timingRingPublished (*pre) && owner->ring()->header.demand.load() == 0,
             "POST cleanup releases demand and retains PRE owner");
    require (post->beginLiveBlind() == StartResult::started, "direct BLIND starts with the same PRE owner");
    post->closeLiveBlind(); post->serviceLiveCompare();
    require (owner->ring()->header.timing.ownerA.load() == a && owner->ring()->header.timing.ownerB.load() == b,
             "LISTEN/BLIND preserve PRE lifetime instead of recreating its mapping");
    SharedRingMapping wrongRate;
    require (! wrongRate.open (key, 96000), "wrong-rate POST is rejected");
    LiveTimingFixtureAccess::retirePreparedFormatRing (*pre);
    require (! LiveTimingFixtureAccess::timingRingPublished (*pre), "format change retires PRE owner");
    require (post->startLiveCompare() == StartResult::preUnavailable, "retired PRE cannot start another session");
    pre->releaseResources(); post->releaseResources();
}
}
int main (int argc, char** argv)
try
{
    require (argc == 2, "choose --state-only or --shared-ring");
    const bool shared = juce::String (argv[1]) == "--shared-ring";
    require (shared || juce::String (argv[1]) == "--state-only", "known test mode");
   #if KIRIN_HYPHA_PRE_DISPLAY
    ValidationStorageSandbox sandbox;
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
   #else
    ReferenceValidationStorageSandbox sandbox;
   #endif
    juce::ScopedJuceInitialiser_GUI initialise;
    int cases = 0;
    for (auto format : { juce::AudioProcessor::wrapperType_VST3,
                        juce::AudioProcessor::wrapperType_AAX,
                       #if JUCE_MAC
                        juce::AudioProcessor::wrapperType_AudioUnit,
                       #endif
                        })
        for (int channels : { 1, 2 })
        {
            if (shared) sharedRing (format, channels); else stateOnly (format, channels);
            ++cases;
        }
    std::cout << "Live publication: PASS " << cases << " format/layout cases " << argv[1] << '\n';
    return EXIT_SUCCESS;
}
catch (const std::exception& error)
{
    std::cerr << "Live publication: " << error.what() << '\n';
    return EXIT_FAILURE;
}
