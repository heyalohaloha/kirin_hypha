#pragma once
#include "reference_runtime_test_support.h"
#include "ReferenceControllerTestAccess.h"
#include "../src/reference_audition/ReferenceTestCommitObserver.h"
#include <condition_variable>
#include <mutex>

namespace hypha::tests::reference_control
{
struct CaptureQueueFacts { unsigned read = 0, write = 0; std::uint64_t generation = 0; };
struct CaptureQueueTag {};
CaptureQueueFacts queueFactsFor (const reference_audition::RuntimeACapture&, CaptureQueueTag);
template <auto Read, auto Write, auto Generation> struct CaptureQueueAccess
{
    friend CaptureQueueFacts queueFactsFor (const reference_audition::RuntimeACapture& capture, CaptureQueueTag)
    { return { (capture.*Read).load (std::memory_order_acquire), (capture.*Write).load (std::memory_order_acquire),
               (capture.*Generation).load (std::memory_order_acquire) }; }
};
template struct CaptureQueueAccess<&reference_audition::RuntimeACapture::readSlot,
    &reference_audition::RuntimeACapture::writeSlot, &reference_audition::RuntimeACapture::continuityGeneration>;
}

namespace
{
// Model the independent OS publisher. This renews authority, never captured PCM or its age.
class LiveRuntimeAuthority final
{
public:
    LiveRuntimeAuthority (juce::File fileIn, ref::RuntimeIdentity identityIn, juce::String recordingIn)
        : file (std::move (fileIn)), identity (std::move (identityIn)), recording (std::move (recordingIn))
    {
        require (renewRuntimeABinding (file, identity, recording), "the fixture OS publishes a normal nine-second A lease");
        worker = std::thread ([this] {
            std::unique_lock<std::mutex> lock (mutex);
            while (! changed.wait_for (lock, std::chrono::seconds (1), [this] { return stopping; }))
            {
                lock.unlock();
                if (! renewRuntimeABinding (file, identity, recording)) failed.store (true, std::memory_order_release);
                else KIRIN_REFERENCE_COMMIT_FOR_TEST(); // An actual fixture OS publication, never an idle worker tick.
                lock.lock();
            }
        });
    }
    ~LiveRuntimeAuthority() { stopAndCheck(); }
    void stopAndCheck()
    {
        { const std::lock_guard<std::mutex> lock (mutex); stopping = true; }
        changed.notify_all();
        if (worker.joinable()) worker.join();
        require (! failed.load (std::memory_order_acquire), "every fixture authority renewal succeeded before stop and join");
    }
private:
    const juce::File file;
    const ref::RuntimeIdentity identity;
    const juce::String recording;
    std::mutex mutex;
    std::condition_variable changed;
    bool stopping = false;
    std::atomic<bool> failed { false };
    std::thread worker;
};

bool liveBlindCaptureCurrent (ref::RuntimeV2Controller& controller, const juce::File& root,
                             const ref::RuntimeIdentity& identity)
{
    const auto now = juce::Time::currentTimeMillis();
    const auto authority = ref::RuntimeABindingRepository (root).load (identity, now);
    const auto receipt = juce::JSON::parse (hypha::tests::reference_control::captureFor (controller, {}).captureFile (identity.runtimeInstanceId));
    const auto capturedAt = static_cast<juce::int64> (receipt["captured_at_ms"]);
    return authority.has_value() && capturedAt > 0 && now >= capturedAt && now - capturedAt <= 10'000
        && static_cast<juce::int64> (receipt["lease_expires_at_ms"]) >= now
        && receipt["binding_id"].toString() == authority->bindingId
        && receipt["recording_id"].toString() == authority->recordingId;
}

void resetForFreshBlindCapture (ref::RuntimeV2Controller& controller, const ref::RuntimeIdentity& identity,
                               const ref::Snapshot& expected)
{
    namespace control = hypha::tests::reference_control;
    const auto deadline = [] { return std::chrono::steady_clock::now() + std::chrono::seconds (4); };
    juce::AudioBuffer<float> stopped (2, 1);
    stopped.clear();
    controller.observeTransport (0, true, false);
    controller.observeAInput (stopped, 0, true, false, true);
    controller.disconnect();
    require (control::waitUntil (deadline(), [&] {
        return controller.snapshot().state == ref::RuntimeState::disconnected
            && control::captureFor (controller, {}).currentAudio() == nullptr;
    }), "the public disconnect receipt proves this capture reset has completed");
    controller.configure (identity, 48'000, 2);
    require (control::waitUntil (deadline(), [&] {
        const auto state = controller.snapshot();
        return state.state == ref::RuntimeState::ready && state.playbackIdentity == expected.playbackIdentity
            && state.manifestRevision == expected.manifestRevision && state.aBindingAvailable
            && control::captureEnabledFor (control::captureFor (controller, {}), {})
            && control::captureFor (controller, {}).currentAudio() == nullptr;
    }), "the same source is ready with a newly armed real capture after configure");
}

void feedFreshBlindCapture (ref::RuntimeV2Controller& controller, const juce::File& root,
                           const ref::RuntimeIdentity& identity, const ref::RuntimeACaptureAudio& source, const char* phase)
{
    namespace control = hypha::tests::reference_control;
    require (source.frameCount == 192'000 && source.sampleRateHz == 48'000 && source.channels == 2
        && control::captureFor (controller, {}).currentAudio() == nullptr
        && control::captureEnabledFor (control::captureFor (controller, {}), {}),
        "a real four-second stereo observation starts only after capture reset and arming");
    const auto expectedPublication = controller.snapshot();
    const auto started = std::chrono::steady_clock::now();
    struct Progress { std::int64_t offset, elapsedMs; control::CaptureQueueFacts queue; bool binding, capture, selected; };
    std::vector<Progress> progress;
    progress.reserve (24);
    for (std::int64_t offset = 0; offset < source.frameCount; offset += 8'192)
    {
        const auto frames = static_cast<int> (std::min<std::int64_t> (8'192, source.frameCount - offset));
        juce::AudioBuffer<float> input (2, frames);
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < frames; ++frame)
                input.setSample (channel, frame, source.interleaved[static_cast<size_t> ((offset + frame) * 2 + channel)]);
        controller.observeTransport (source.startSample + offset, true, true);
        controller.observeAInput (input, source.startSample + offset, true, true, true);
        const auto state = controller.snapshot();
        progress.push_back ({ offset, std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - started).count(),
            control::queueFactsFor (control::captureFor (controller, {}), {}), state.aBindingAvailable, state.aCaptureAvailable, state.bSelected });
        const auto next = started + std::chrono::nanoseconds ((offset + frames) * 1'000'000'000 / source.sampleRateHz);
        if (std::chrono::steady_clock::now() < next) std::this_thread::sleep_until (next);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (4);
    std::shared_ptr<const ref::RuntimeACaptureAudio> completed;
    const bool captureCompleted = control::waitUntil (deadline, [&] {
        completed = control::captureFor (controller, {}).currentAudio();
        return completed != nullptr;
    });
    std::uint64_t refreshTicket = 0;
    if (captureCompleted)
    {
        require (completed->frameCount == source.frameCount && completed->sampleRateHz == source.sampleRateHz
            && completed->channels == source.channels && completed->startSample == source.startSample
            && completed->cuePcmSha256 == source.cuePcmSha256 && completed->interleaved == source.interleaved,
            "the first completed capture is the exact real expected PCM, without waiting for a replacement");
        if (std::chrono::steady_clock::now() < deadline)
        {
            refreshTicket = ref::requestReferenceTestWorkspaceRefresh (&controller);
            require (refreshTicket != 0, "one ordinary worker read follows actual PCM completion");
        }
    }
    const bool prepared = refreshTicket != 0 && control::waitUntil (deadline, [&] {
        require (control::captureFor (controller, {}).currentAudio() == completed,
                 "the completed PCM remains published throughout Blind preparation");
        if (! ref::referenceTestWorkspaceRefreshConsumed (refreshTicket)) return false;
        const auto state = controller.snapshot();
        return state.state == ref::RuntimeState::ready && state.aCaptureAvailable && state.blindEligible
            && state.manifestRevision == expectedPublication.manifestRevision
            && state.playbackIdentity == expectedPublication.playbackIdentity
            && liveBlindCaptureCurrent (controller, root, identity);
    });
    if (! prepared)
    {
        const auto current = control::captureFor (controller, {}).currentAudio();
        const auto file = control::captureFor (controller, {}).captureFile (identity.runtimeInstanceId);
        std::cerr << "CAPTURE_STAGE phase=" << phase << " completed=" << captureCompleted << " atomicPCM=" << (current != nullptr)
                  << " samePCM=" << (current == completed) << " frames=" << (current ? current->frameCount : 0)
                  << " receiptFile=" << file.existsAsFile() << " refreshTicket=" << refreshTicket
                  << " refreshConsumed=" << ref::referenceTestWorkspaceRefreshConsumed (refreshTicket) << '\n';
        if (refreshTicket != 0) ref::cancelReferenceTestWorkspaceRefresh (&controller, refreshTicket);
        for (const auto& item : progress)
            std::cerr << "CAPTURE_PROGRESS phase=" << phase << " frame=" << item.offset << " elapsedMs=" << item.elapsedMs
                      << " read=" << item.queue.read << " write=" << item.queue.write << " generation=" << item.queue.generation
                      << " binding=" << item.binding << " capture=" << item.capture << " B=" << item.selected << '\n';
        const auto state = controller.snapshot();
        std::cerr << "CAPTURE_FINAL phase=" << phase << " state=" << static_cast<int> (state.state) << " revision=" << state.manifestRevision
                  << " binding=" << state.aBindingAvailable << " capture=" << state.aCaptureAvailable
                  << " eligible=" << state.blindEligible << " rejection=" << state.rejectionCode << '\n';
    }
    require (prepared, "the actual PCM observation prepares Blind within the four-second preparation deadline");
    const auto captured = control::captureFor (controller, {}).currentAudio();
    require (captured && captured == completed && captured->frameCount == source.frameCount && captured->startSample == source.startSample
        && captured->cuePcmSha256 == source.cuePcmSha256 && captured->interleaved == source.interleaved
        && liveBlindCaptureCurrent (controller, root, identity), "fresh capture has the real expected PCM and an unextended live lease");
    std::cout << "Fresh Blind PCM capture: " << phase << ", 192000 frames, exact PCM and live authority PASS\n";
}
}
