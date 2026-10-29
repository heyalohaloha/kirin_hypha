#include "../src/PluginProcessor.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "ReferenceValidationStorageSandbox.h"
#include "LiveTimingFixtureAccess.h"
#include "PublicationStorageContract.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "TestProcessorStorage.h"
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
    hypha::tests::prepareFixtureProcessor (*processor, 48000, frames);
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
    const auto listenMissing = post->startLiveCompare();
    require (listenMissing == StartResult::preUnavailable
        && ! post->liveCompareStatus().active, "missing PRE fails explicitly after POST reaches open");
    const auto blindMissing = post->beginLiveBlind();
    require (blindMissing == StartResult::preUnavailable
        && ! post->liveCompareStatus().active, "missing PRE also refuses direct BLIND before starting its trial");
    require (hypha::live_compare_ui::startNeedsPreChoice (listenMissing)
        && hypha::live_compare_ui::startNeedsPreChoice (blindMissing),
        "a stopped paired PRE opens the candidate menu for LISTEN and BLIND");
    require (juce::String (hypha::live_compare_ui::preUnavailableNotice()) == "PRE not running; choose again",
        "a stopped paired PRE has a short selection notice");
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

void duplicateState (juce::AudioProcessor::WrapperType format, int channels)
{
    Clock clock;
    auto pre = prepare (Processor::Role::Pre, format, channels, clock);
    auto post = prepare (Processor::Role::Post, format, channels, clock);
    selectPair (*pre, *post, clock, channels);
    const auto originalPre = pre->instanceId(), originalPost = post->instanceId();
    juce::MemoryBlock preState, postState;
    pre->getStateInformation (preState); post->getStateInformation (postState);
    const auto recall = [&] (Processor::Role role, const juce::MemoryBlock& state)
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (format);
        auto processor = std::make_unique<Processor> (role);
        auto layout = processor->getBusesLayout();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.inputBuses.set (0, set); layout.outputBuses.set (0, set);
        require (processor->setBusesLayout (layout), "copied layout negotiates");
        processor->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        processor->setPlayHead (&clock);
        hypha::tests::prepareFixtureProcessor (*processor, 48000, frames);
        LiveTimingFixtureAccess::enableFixtureWrites (*processor);
        require (hypha::tests::fixtureStorageReady (*processor), "copied identity is claimed before publication");
        if (role == Processor::Role::Pre)
            require (LiveTimingFixtureAccess::timingRingPublished (*processor), "recalled PRE publishes after prior ring retirement");
        return processor;
    };
    auto copyPre = recall (Processor::Role::Pre, preState);
    auto copyPost = recall (Processor::Role::Post, postState);
    const auto copiedPre = copyPre->instanceId(), copiedPost = copyPost->instanceId();
    require (copiedPre != originalPre && copiedPost != originalPost, "live state copies receive distinct IDs");
    require (pre->instanceId() == originalPre && post->instanceId() == originalPost,
             "original owners keep their IDs");
    require (copyPost->pairedPreInstanceId().isEmpty(), "copied POST must not inherit original exact PRE");
    require (post->pairedPreInstanceId() == originalPre, "copy cannot change original pair");
    for (auto* processor : { pre.get(), post.get(), copyPre.get(), copyPost.get() })
    {
        normalBlock (*processor, channels);
        require (! processor->liveCompareStatus().active, "copy never starts audition");
    }
    selectPair (*copyPre, *copyPost, clock, channels);
    require (post->pairedPreInstanceId() == originalPre && copyPost->pairedPreInstanceId() == copiedPre,
             "two explicitly selected pairs stay independent");
    // A second, late host state callback cannot put the copied POST back onto the original PRE.
    copyPost->setStateInformation (postState.getData(), static_cast<int> (postState.getSize()));
    require (copyPost->instanceId() == copiedPost && copyPost->pairedPreInstanceId().isEmpty(),
             "late copy retains claimed ID and clears imported authority");
    selectPair (*copyPre, *copyPost, clock, channels);
    copyPre->getStateInformation (preState); copyPost->getStateInformation (postState);
    juce::MemoryBlock originalPreState, originalPostState;
    pre->getStateInformation (originalPreState); post->getStateInformation (originalPostState);
    // A real format rebuild destroys/join old engine before reacquiring the same processor ID.
    pre->prepareToPlay (96000, frames);
    LiveTimingFixtureAccess::enableFixtureWrites (*pre);
    require (pre->instanceId() == originalPre, "format rebuild retains original identity");
    copyPost.reset(); copyPre.reset(); post.reset(); pre.reset();
    // Reopen both construction orders from the repaired, distinct saved states. A POST restored
    // first must retain its exact locator, never discover the other same-name PRE instead.
    for (bool reversed : { false, true })
    {
        if (reversed)
        {
            copyPost = recall (Processor::Role::Post, postState);
            pre = recall (Processor::Role::Pre, originalPreState);
            post = recall (Processor::Role::Post, originalPostState);
            copyPre = recall (Processor::Role::Pre, preState);
        }
        else
        {
            pre = recall (Processor::Role::Pre, originalPreState);
            post = recall (Processor::Role::Post, originalPostState);
            copyPre = recall (Processor::Role::Pre, preState);
            copyPost = recall (Processor::Role::Post, postState);
        }
        require (pre->instanceId() == originalPre && post->instanceId() == originalPost
            && copyPre->instanceId() == copiedPre && copyPost->instanceId() == copiedPost,
                 "reopen order does not change repaired saved identities");
        require (post->pairedPreInstanceId() == originalPre && copyPost->pairedPreInstanceId() == copiedPre,
                 "reopen restores only each saved exact PRE");
        require (! post->liveCompareStatus().active && ! copyPost->liveCompareStatus().active,
                 "reopen never restores audition permission");
        copyPost.reset(); copyPre.reset(); post.reset(); pre.reset();
    }
}

void identityIoFailure (const juce::File& sandbox)
{
    const auto obstruction = sandbox.getChildFile ("tmp/kirin/.instance_identity");
    require (obstruction.getParentDirectory().createDirectory().wasOk(), "owned identity root");
    {
        juce::FileOutputStream stream (obstruction);
        require (stream.openedOk() && stream.write ("blocked", 7), "owned identity IO failure fixture");
        stream.flush();
        require (stream.getStatus().wasOk(), "owned obstruction was written");
    }
    Clock clock;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (Processor::Role::Pre);
    processor.setPlayHead (&clock);
    hypha::tests::prepareFixtureProcessor (processor, 48000, frames);
    const auto restoredId = processor.instanceId();
    LiveTimingFixtureAccess::enableFixtureWrites (processor);
    require (! hypha::tests::fixtureStorageReady (processor)
        && ! LiveTimingFixtureAccess::timingRingPublished (processor),
             "identity IO failure cannot publish unclaimed transport");
    require (processor.instanceId() == restoredId, "IO failure preserves recovery identity");
    require (obstruction.deleteFile(), "remove owned obstruction");
    LiveTimingFixtureAccess::enableFixtureWrites (processor);
    require (hypha::tests::fixtureStorageReady (processor)
        && processor.instanceId() == restoredId, "recovery claims retained identity");
}

void identityBoundary()
{
    Clock clock;
    const auto create = [&] (const juce::String& id, Processor::Role role = Processor::Role::Pre)
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        auto processor = std::make_unique<Processor> (role);
        juce::XmlElement xml ("KirinHyphaState");
        xml.setAttribute ("instance_id", id);
        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary (xml, state);
        processor->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        processor->setPlayHead (&clock);
        hypha::tests::prepareFixtureProcessor (*processor, 48000, frames);
        LiveTimingFixtureAccess::enableFixtureWrites (*processor);
        require (hypha::tests::fixtureStorageReady (*processor), "boundary identity claimed");
        return processor;
    };
    const auto prefix = juce::String::repeatedString ("x", 63), full = prefix + "y";
    auto first = create (prefix), second = create (full);
    require (first->instanceId() == prefix && second->instanceId() == full,
             "complete readback keeps 64-byte identity distinct from 63-byte prefix");
    require (LiveTimingFixtureAccess::timingRingPublished (*first)
        && LiveTimingFixtureAccess::timingRingPublished (*second), "both distinct full keys publish");
    auto copy = create (full);
    require (copy->instanceId() != full && copy->instanceId() != prefix, "full identity copy rekeys");
    juce::MemoryBlock state; second->getStateInformation (state);
    auto saved = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    require (saved && saved->getStringAttribute ("instance_id") == full, "saved identity is not shortened");
    auto post = create ("boundary-post", Processor::Role::Post);
    selectPair (*first, *post, clock, 2);
    require (post->pairedPreInstanceId() == prefix, "63-byte candidate selects its exact instance");
    selectPair (*second, *post, clock, 2);
    require (post->pairedPreInstanceId() == full, "64-byte candidate cannot select prefix instance");
    bool hasPrefix = false, hasFull = false;
    for (const auto& candidate : post->enumeratePreCandidates())
    {
        hasPrefix = hasPrefix || candidate.instanceId == prefix;
        hasFull = hasFull || candidate.instanceId == full;
    }
    require (hasPrefix && hasFull, "actual candidate export keeps both complete IDs");
    juce::String project, selected;
    require (post->pairedPreLocator (project, selected) && selected == full,
             "complete exact locator returns the selected long PRE");
    hypha::local_blind::ExactPairBinding unsupported;
    require (! post->localBlindPairBinding (unsupported),
             "legacy Local Blind DTO refuses long identity instead of selecting its prefix");
}
}
int main (int argc, char** argv)
try
{
    require (argc == 2, "choose --state-only, --shared-ring or --storage-only");
    const bool shared = juce::String (argv[1]) == "--shared-ring";
    const bool storage = juce::String (argv[1]) == "--storage-only";
    const bool duplicate = juce::String (argv[1]) == "--duplicate";
    require (shared || storage || duplicate || juce::String (argv[1]) == "--state-only", "known test mode");
   #if KIRIN_HYPHA_PRE_DISPLAY
    ValidationStorageSandbox sandbox;
   #else
    ReferenceValidationStorageSandbox sandbox;
   #endif
    // Guide transport is compiled for both roles; PRE_DISPLAY only selects the PRE UI.
    // This fixture also exercises the explicit Guide transport override.
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
    hypha::tests::requirePublicationStorage (sandbox.directory());
    if (storage)
    {
        hypha::tests::verifyPublicationStorage (sandbox.directory());
        std::cout << "Publication storage: PASS PRE/POST leases and cleanup\n";
        return EXIT_SUCCESS;
    }
    juce::ScopedJuceInitialiser_GUI initialise;
    if (duplicate) { identityIoFailure (sandbox.directory()); identityBoundary(); }
    int cases = 0;
    for (auto format : { juce::AudioProcessor::wrapperType_VST3,
                        juce::AudioProcessor::wrapperType_AAX,
                       #if JUCE_MAC
                        juce::AudioProcessor::wrapperType_AudioUnit,
                       #endif
                        })
        for (int channels : { 1, 2 })
        {
            if (duplicate) duplicateState (format, channels);
            else if (shared) sharedRing (format, channels); else stateOnly (format, channels);
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
