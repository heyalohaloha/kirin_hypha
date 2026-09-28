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
    liveCompare.postLevel.configure (preparedFormat.sampleRate);
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
    // INV-LC9: a PRE that is one channel of a multi-mono set says so, and POST refuses it with the
    // reason. The set is complete by now: writes are enabled well after the host creates it.
    const auto source = aaxMultiMonoMember() ? hypha::live_compare::ringSourceMultiMono : 0u;
    if (mapping->create (key, static_cast<std::uint32_t> (preparedFormat.sampleRate), source))
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
    if ((mapping->ring()->header.source.load (std::memory_order_acquire) & hypha::live_compare::ringSourceMultiMono) != 0)
        return StartResult::preMultiMono;
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

hypha::live_compare::MatchResult KirinHyphaProcessorBase::measureLiveCompare()
{
    // Message thread: reads the POST history and PRE's ring through atomics only.
    hypha::live_compare::MatchResult result;
    if (role != Role::Post || ! liveCompare.sessionActive.load (std::memory_order_acquire))
        return result;
    const auto* mapping = liveCompare.ring.control();
    if (mapping == nullptr || mapping->ring() == nullptr)
        return result;
    return hypha::live_compare::computeMatch (*mapping->ring(), liveCompare.renderer, mapping->rate());
}

// Message thread. A plan that needs approval takes the user's choice: lower POST with PRE at its
// level, or raise PRE only up to the ceiling. The guard's ceiling is stored before the gains.
bool KirinHyphaProcessorBase::applyLiveCompareMatch (const hypha::live_compare::MatchPlan& plan,
                                                     hypha::live_compare::MatchChoice choice)
{
    using hypha::live_compare::MatchChoice;
    if (role != Role::Post || ! liveCompare.sessionActive.load (std::memory_order_acquire)
        || plan.needsApproval == (choice == MatchChoice::basis))
        return false;
    const bool lower = choice == MatchChoice::lowerPost;
    const double preDb = lower ? 0.0 : plan.preGainDb;
    const double postDb = lower ? plan.lowerPostGainDb : plan.postGainDb;
    const auto linear = [] (double db) { return static_cast<float> (std::pow (10.0, db / 20.0)); };
    liveCompare.ceilingLinear.store (linear (plan.ceilingDbtp), std::memory_order_release);
    setLiveCompareGain (linear (preDb));
    liveCompare.postTarget.store (std::min (1.0f, linear (postDb)), std::memory_order_release);
    return true;
}

// Message thread (INV-LC16): AUTO moves PRE's gain only. The ceiling and POST stay as the explicit
// MATCH approved them; the Audio Thread ramps the new gain over 50 ms.
bool KirinHyphaProcessorBase::followLiveCompareGain (double preDb)
{
    if (role != Role::Post || ! liveCompare.sessionActive.load (std::memory_order_acquire) || ! std::isfinite (preDb))
        return false;
    setLiveCompareGain (static_cast<float> (std::pow (10.0, preDb / 20.0)));
    return true;
}

// INV-LC8: Pro Tools says its delay compensation as a whole is on or off (JUCE patch 0009, AAX
// only, off the Audio Thread). While it is off the positions POST sees are not compensated, so no
// clock can prove the correspondence: POST sounds and PRE waits until it is on again.
void KirinHyphaProcessorBase::kirinHostDelayCompensationStateChanged (bool enabled)
{
    liveCompare.compensationOff.store (! enabled, std::memory_order_release);
}

// INV-LC9: Pro Tools names this instance's group once, before the first prepare (JUCE patch 0010).
void KirinHyphaProcessorBase::kirinHostInstanceGroup (juce::uint64 group, bool valid)
{
    liveCompare.aaxGroup.assign (static_cast<std::uint64_t> (group), valid);
}

// Message thread, the explicit RETURN: POST rises back to its normal level over half a second.
void KirinHyphaProcessorBase::returnLiveComparePostToNormal() noexcept
{
    liveCompare.postTarget.store (1.0f, std::memory_order_release);
}

bool KirinHyphaProcessorBase::takeLiveCompareGuardTrip() noexcept
{
    return liveCompare.guardTripped.exchange (false, std::memory_order_acq_rel);
}

// Message thread (INV-LC7): the content offset of the latest proven window.
hypha::live_compare::OffsetEstimate KirinHyphaProcessorBase::measureLiveCompareOffset()
{
    if (role != Role::Post || ! liveCompare.sessionActive.load (std::memory_order_acquire))
        return {};
    const auto* mapping = liveCompare.ring.control();
    if (mapping == nullptr || mapping->ring() == nullptr)
        return {};
    return hypha::live_compare::measureOffset (*mapping->ring(), liveCompare.renderer);
}

// Message thread (INV-LC10): the offset jumped with unchanged clocks, a latency change the DAW did
// not compensate. POST sounds, PRE stays selected, until playback stops and restarts.
void KirinHyphaProcessorBase::holdLiveCompareForContentJump() noexcept
{
    liveCompare.contentHold.store (true, std::memory_order_release);
}

std::uint32_t KirinHyphaProcessorBase::liveComparePlaybackRun() const noexcept
{
    return liveCompare.playbackRun.load (std::memory_order_acquire);
}

bool KirinHyphaProcessorBase::takeLiveComparePreWait() noexcept
{
    return liveCompare.preWaitSeen.exchange (false, std::memory_order_acq_rel);
}

bool KirinHyphaProcessorBase::liveCompareSupported() const noexcept
{
    // INV-LC9: AAX offers the live compare on stereo instances and on the only instance of its
    // group, as on a mono track, so PRE and POST never mix across the channels of a multi-mono set.
    return role == Role::Post && stereoWorkflowsSupported() && ! aaxMultiMonoMember()
        && hypha::live_compare::sharedRingAvailable();
}

// INV-LC9: a mono AAX instance that the host does not show to be the only one of its group: one
// channel of a multi-mono set, or a host that names no groups. Pro Tools processes the channels of
// a set on parallel threads (G1 record, section 10), so no instance can switch them all in a block.
bool KirinHyphaProcessorBase::aaxMultiMonoMember() const noexcept
{
    return wrapperType == wrapperType_AAX && getTotalNumInputChannels() < 2 && ! liveCompare.aaxGroup.alone();
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
    status.postTarget = liveCompare.postTarget.load (std::memory_order_acquire);
    status.contentHeld = liveCompare.contentHold.load (std::memory_order_acquire);
    status.compensationOff = liveCompare.compensationOff.load (std::memory_order_acquire);
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

    // INV-LC10: a playback run starts at play after stop; a content-jump hold ends with its run.
    if (block.playing && ! liveCompare.wasPlaying)
        liveCompare.playbackRun.fetch_add (1, std::memory_order_acq_rel);
    if (! block.playing)
        liveCompare.contentHold.store (false, std::memory_order_release);
    liveCompare.wasPlaying = block.playing;
    const bool contentHeld = liveCompare.contentHold.load (std::memory_order_acquire);
    // INV-LC8: a change of the host's delay compensation moves POST's positions; K is proven again.
    const bool compensationOff = liveCompare.compensationOff.load (std::memory_order_acquire);
    if (compensationOff != liveCompare.compensationWasOff)
    {
        block.afterGap = true;
        liveCompare.compensationWasOff = compensationOff;
    }
    const bool preSelected = liveCompare.preSelected.load (std::memory_order_acquire);
    const float postTarget = liveCompare.postTarget.load (std::memory_order_acquire);
    bool rendered = false;
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
        rendered = true;
        const auto report = liveCompare.renderer.render (*ring, mapping.key(), mapping.rate(), block,
                                                         buffer.getArrayOfWritePointers(), channels,
                                                         preSelected && ! contentHeld && ! compensationOff,
                                                         liveCompare.gain.load (std::memory_order_acquire),
                                                         liveCompare.postLevel, postTarget,
                                                         liveCompare.ceilingLinear.load (std::memory_order_acquire));
        if (report.guardTripped)
        {
            // The guard ends PRE for this selection; the user selects it again.
            liveCompare.preSelected.store (false, std::memory_order_release);
            liveCompare.interrupted.store (true, std::memory_order_release);
            liveCompare.guardTripped.store (true, std::memory_order_release);
        }
        liveCompare.verdict.store (static_cast<std::uint8_t> (report.verdict), std::memory_order_release);
        liveCompare.preAudible.store (report.preAudible, std::memory_order_release);
        const bool preHeldBack = report.preWaiting
            || (preSelected && (contentHeld || compensationOff) && ! report.guardTripped);
        liveCompare.preWaiting.store (preHeldBack, std::memory_order_release);
        if (preHeldBack)
            liveCompare.preWaitSeen.store (true, std::memory_order_release);
    });
    // Out of a session POST keeps an approved attenuation until the explicit RETURN (INV-LC14).
    // Offline render, bypass and another audition's output are never touched.
    if (! rendered && usable && ! outputTaken)
        liveCompare.postLevel.apply (buffer.getArrayOfWritePointers(), channels, frames, postTarget);
}
