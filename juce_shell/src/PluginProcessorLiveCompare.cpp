#include "PluginProcessor.h"

#include <chrono>
#include <cmath>
#include <thread>

// Live PRE/POST compare, stage 1 (AGENTS R-12, INV-LC1 to INV-LC3). PRE owns a ring for its own
// identity; POST opens it for an explicit user session and plays PRE only where the correspondence
// is proven. Everything here that maps, names or waits runs on the message thread.
namespace
{
using hypha::live_compare::SharedRingMapping;
using Slot = hypha::local_blind::RtPublicationSlot<SharedRingMapping>;

// Non-RT: an Audio Thread reader leaves the slot within one callback; wait for it briefly.
bool collectWithin (Slot& slot)
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        if (slot.collect())
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return false;
}

bool publishMapping (Slot& slot, std::unique_ptr<SharedRingMapping> mapping)
{
    slot.retire();
    return collectWithin (slot) && slot.publish (std::move (mapping));
}

std::uint64_t steadyNanos() noexcept
{
    return static_cast<std::uint64_t> (std::chrono::duration_cast<std::chrono::nanoseconds> (
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

void KirinHyphaProcessorBase::startPreparedFormatServices()
{
    startLocalBlindCaptureForPreparedFormat();
    prepareLiveCompareForPreparedFormat();
}

void KirinHyphaProcessorBase::prepareLiveCompareForPreparedFormat()
{
    // PRE stamps a ring for its identity and the prepared rate once its writes are enabled. Any
    // ring it still owns closes first: the name is never re-stamped under a live mapping, and a
    // POST holding the old one sees its owner go.
    if (role != Role::Pre || ! stereoWorkflowsSupported() || persistInstanceId.isEmpty())
        return;
    liveCompare.ring.retire();
    if (! collectWithin (liveCompare.ring))
        return;
    auto mapping = std::make_unique<SharedRingMapping>();
    const auto key = hypha::live_compare::pairKeyForPreInstance (persistInstanceId.toStdString());
    if (mapping->create (key, static_cast<std::uint32_t> (preparedFormat.sampleRate)))
        liveCompare.ring.publish (std::move (mapping));
}

void KirinHyphaProcessorBase::stopLiveCompareForFormatChange()
{
    if (role == Role::Post)
        stopLiveCompare();
    else
    {
        liveCompare.ring.retire();
        collectWithin (liveCompare.ring);
    }
}

hypha::live_compare::StartResult KirinHyphaProcessorBase::startLiveCompare()
{
    using hypha::live_compare::StartResult;
    if (role != Role::Post)
        return StartResult::notPost;
    if (! writesEnabled.load (std::memory_order_acquire))
        return StartResult::notReady;
    if (! liveCompareSupported())
        return StartResult::unsupportedLayout;
    const auto pre = pairedPreInstanceId();
    if (pre.isEmpty())
        return StartResult::noPair;
    stopLiveCompare();
    auto mapping = std::make_unique<SharedRingMapping>();
    const auto key = hypha::live_compare::pairKeyForPreInstance (pre.toStdString());
    if (! mapping->open (key, static_cast<std::uint32_t> (preparedFormat.sampleRate)))
        return StartResult::preUnavailable;
    liveCompare.renderer.prepare (juce::jmax (getBlockSize(), 16384), preparedFormat.sampleRate);
    liveCompare.gain.store (1.0f, std::memory_order_release); // each session approves its own MATCH
    mapping->ring()->header.demand.store (1, std::memory_order_release);
    if (! publishMapping (liveCompare.ring, std::move (mapping)))
        return StartResult::notReady;
    liveCompare.interrupted.store (false, std::memory_order_release);
    liveCompare.sessionActive.store (true, std::memory_order_release);
    return StartResult::started;
}

void KirinHyphaProcessorBase::stopLiveCompare()
{
    if (role != Role::Post)
        return;
    liveCompare.sessionActive.store (false, std::memory_order_release);
    liveCompare.preSelected.store (false, std::memory_order_release);
    if (auto* mapping = liveCompare.ring.control(); mapping != nullptr && mapping->ring() != nullptr)
        mapping->ring()->header.demand.store (0, std::memory_order_release);
    liveCompare.ring.retire();
    collectWithin (liveCompare.ring);
    liveCompare.preAudible.store (false, std::memory_order_release);
    liveCompare.preWaiting.store (false, std::memory_order_release);
}

void KirinHyphaProcessorBase::selectLiveComparePre (bool pre) noexcept
{
    if (role == Role::Post && liveCompare.sessionActive.load (std::memory_order_acquire))
    {
        liveCompare.interrupted.store (false, std::memory_order_release);
        liveCompare.preSelected.store (pre, std::memory_order_release);
    }
}

void KirinHyphaProcessorBase::setLiveCompareGain (float linear) noexcept
{
    if (std::isfinite (linear) && linear > 0.0f && linear <= 16.0f)
        liveCompare.gain.store (linear, std::memory_order_release);
}

hypha::live_compare::MatchResult KirinHyphaProcessorBase::matchLiveCompare()
{
    // Message thread: reads the POST history and PRE's ring through atomics only.
    hypha::live_compare::MatchResult result;
    if (role != Role::Post || ! liveCompare.sessionActive.load (std::memory_order_acquire))
        return result;
    const auto* mapping = liveCompare.ring.control();
    if (mapping == nullptr || mapping->ring() == nullptr)
        return result;
    result = hypha::live_compare::computeMatch (*mapping->ring(), liveCompare.renderer, mapping->rate());
    if (result.ok())
        setLiveCompareGain (static_cast<float> (std::pow (10.0, result.appliedDb / 20.0)));
    return result;
}

bool KirinHyphaProcessorBase::takeLiveComparePreWait() noexcept
{
    return liveCompare.preWaitSeen.exchange (false, std::memory_order_acq_rel);
}

bool KirinHyphaProcessorBase::liveCompareSupported() const noexcept
{
    // AAX multi-mono runs one mono instance per channel, and a mono instance cannot tell that apart
    // from a mono track. Until every channel switches in the same block (INV-LC9), AAX offers the
    // live compare on stereo instances only, so PRE and POST never mix across channels.
    const bool aaxMono = wrapperType == wrapperType_AAX && getTotalNumInputChannels() < 2;
    return role == Role::Post && stereoWorkflowsSupported() && ! aaxMono
        && hypha::live_compare::sharedRingAvailable();
}

// Message thread. A session belongs to the PRE ring it opened: a changed or cleared pair, or a PRE
// that closed that ring (re-prepared, removed), ends it. Returns true when it ended the session.
bool KirinHyphaProcessorBase::serviceLiveCompare()
{
    if (role != Role::Post || ! liveCompare.sessionActive.load (std::memory_order_acquire))
        return false;
    const auto* mapping = liveCompare.ring.control();
    const auto pre = pairedPreInstanceId();
    const bool current = mapping != nullptr && mapping->ring() != nullptr && pre.isNotEmpty()
        && hypha::live_compare::pairKeyForPreInstance (pre.toStdString()) == mapping->key()
        && mapping->ring()->header.ownerClosed.load (std::memory_order_acquire) == 0;
    if (current)
        return false;
    stopLiveCompare();
    return true;
}

hypha::live_compare::Status KirinHyphaProcessorBase::liveCompareStatus() const noexcept
{
    hypha::live_compare::Status status;
    status.active = liveCompare.sessionActive.load (std::memory_order_acquire);
    status.preSelected = liveCompare.preSelected.load (std::memory_order_acquire);
    status.preAudible = liveCompare.preAudible.load (std::memory_order_acquire);
    status.preWaiting = liveCompare.preWaiting.load (std::memory_order_acquire);
    status.interrupted = liveCompare.interrupted.load (std::memory_order_acquire);
    status.verdict = static_cast<hypha::live_compare::Verdict> (liveCompare.verdict.load (std::memory_order_acquire));
    status.gain = liveCompare.gain.load (std::memory_order_acquire);
    return status;
}

// Audio Thread. Both roles advance their continuous clock and gap detector on every callback so
// that the clock semantics hold whether or not a session is active.
void KirinHyphaProcessorBase::processLiveCompare (juce::AudioBuffer<float>& buffer,
                                                  const hypha::HostProcessClock& clock,
                                                  bool bypassed, bool nonRealtimeMode,
                                                  bool outputTaken) noexcept
{
    const int frames = buffer.getNumSamples();
    const auto continuous = liveCompare.clock.next (clock.auxiliary, frames);
    hypha::live_compare::BlockClock block;
    block.clock = continuous.samples;
    block.clockValid = continuous.valid;
    block.project = clock.positionSamples;
    block.projectValid = clock.hasPosition;
    block.playing = clock.playing;
    block.frames = frames;
    block.afterGap = liveCompare.gaps.observe (steadyNanos(), frames, preparedFormat.sampleRate);
    const int channels = buffer.getNumChannels();
    const bool usable = ! bypassed && ! nonRealtimeMode && channels > 0 && channels <= 2;

    if (role == Role::Pre)
    {
        liveCompare.ring.withRealtime ([&] (SharedRingMapping& mapping)
        {
            if (auto* ring = mapping.ring(); ring != nullptr && usable)
                liveCompare.feeder.feed (*ring, block, buffer.getArrayOfReadPointers(), channels);
        });
        return;
    }

    const bool preSelected = liveCompare.preSelected.load (std::memory_order_acquire);
    liveCompare.ring.withRealtime ([&] (SharedRingMapping& mapping)
    {
        auto* ring = mapping.ring();
        if (ring == nullptr)
            return;
        if (! usable || outputTaken)
        {
            // Offline render, bypass, a layout change or another audition keeps POST; PRE needs
            // selecting again.
            liveCompare.renderer.silenceTransition();
            if (preSelected)
            {
                liveCompare.preSelected.store (false, std::memory_order_release);
                liveCompare.interrupted.store (true, std::memory_order_release);
            }
            liveCompare.preAudible.store (false, std::memory_order_release);
            liveCompare.preWaiting.store (false, std::memory_order_release);
            return;
        }
        const auto report = liveCompare.renderer.render (*ring, mapping.key(), mapping.rate(), block,
                                                         buffer.getArrayOfWritePointers(), channels,
                                                         preSelected,
                                                         liveCompare.gain.load (std::memory_order_acquire));
        liveCompare.verdict.store (static_cast<std::uint8_t> (report.verdict), std::memory_order_release);
        liveCompare.preAudible.store (report.preAudible, std::memory_order_release);
        liveCompare.preWaiting.store (report.preWaiting, std::memory_order_release);
        if (report.preWaiting)
            liveCompare.preWaitSeen.store (true, std::memory_order_release);
    });
}
