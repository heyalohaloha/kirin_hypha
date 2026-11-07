#pragma once
#include "ReferenceControllerTestAccess.h"
#include "../src/reference_audition/ReferenceTestCommitObserver.h"
#include "reference_whole_song_fixture.h"
#include <algorithm>
#include <iostream>
#include <thread>

namespace
{
// A synthetic host must not mistake a capture from before configure for its new
// input. Pause before capture service, then observe the real next worker cycle.
inline void configureWholeSongCapture (ref::ReferenceComparisonController& controller,
                                      const ref::RuntimeIdentity& identity, double rate, int channels)
{
    namespace control = hypha::tests::reference_control;
    auto& version = control::roleFor (controller, {1});
    require (ref::pauseReferenceTestCaptureWorker (&version, 30000), "the real Version worker reaches the configuration barrier");
    struct Resume { const void* worker; ~Resume() { ref::resumeReferenceTestCaptureWorker (worker); } } resume {&version};
    controller.configure (identity, rate, channels);
    const auto ticket = ref::requestReferenceTestWorkspaceRefresh (&version);
    require (ticket != 0, "one real configuration refresh is requested");
    ref::resumeReferenceTestCaptureWorker (&version);
    require (control::waitUntil (std::chrono::steady_clock::now() + std::chrono::minutes (5), [&] {
        const auto& capture = control::captureFor (version, {});
        return ref::referenceTestWorkspaceRefreshConsumed (ticket)
            && control::captureEnabledFor (capture, {}) && capture.currentAudio() == nullptr;
    }), "fresh host PCM starts only after the configured worker has reset and armed its capture");
}

// The test owns PCM and transport. Consumer acknowledgements provide backpressure;
// a new four-second capture ends input, not a fixed block count or elapsed time.
class WholeSongInputClock
{
public:
    template <class Predicate>
    bool waitFor (ref::ReferenceComparisonController& controller,
                  const WholeSongFixture& fixture, int& cursor, Predicate ready, int channels = 2, int watchdogSeconds = 300)
    {
        namespace control = hypha::tests::reference_control;
        const auto complete = [&] { return ready (controller.snapshot()); };
        const auto failed = [&] {
            std::cerr << "WHOLE_SONG_WAIT cursor=" << cursor;
            for (int slot = 1; slot <= 3; ++slot)
            {
                auto& role = control::roleFor (controller, {slot});
                const auto state = role.snapshot(); const auto& input = control::captureFor (role, {});
                const auto audio = input.currentAudio();
                std::cerr << " role=" << slot << " state=" << int (state.state) << " reason=" << state.rejectionCode
                    << " available=" << state.aCaptureAvailable << " enabled=" << control::captureEnabledFor (input, {})
                    << " frames=" << (audio ? audio->frameCount : 0) << " buffered=" << state.auditionBuffered
                    << " worker=" << ref::referenceTestWorkerStage (&role);
            }
            std::cerr << std::endl; return false;
        };
        const auto& capture = control::captureFor (control::roleFor (controller, {1}), {});
        const auto watchdog = [watchdogSeconds] { return std::chrono::steady_clock::now() + std::chrono::seconds (watchdogSeconds); };
        if (! control::waitUntil (watchdog(), [&] {
            return complete() || control::captureEnabledFor (capture, {});
        })) return failed();
        const auto previousCapture = capture.currentAudio();
        const auto origin = std::chrono::steady_clock::now();
        auto stalledAt = watchdog();
        std::int64_t suppliedFrames = 0;
        int remainingInBurst = 0;
        for (;;)
        {
            if (remainingInBurst == 0)
            {
                if (! control::waitUntil (stalledAt, [&] {
                    for (int role = 1; role <= 3; ++role)
                        if (! control::captureFor (control::roleFor (controller, {role}), {})
                                .observationQueueDrained()) return false;
                    return true;
                })) return failed();
                stalledAt = watchdog(); // The preceding PCM has been consumed.
                if (complete()) return true;
                // Consumption precedes alignment and loudness measurement. Keep
                // this capture stable until that worker finishes, even on a slow host.
                // The watchdog detects a stall; it never declares success.
                if (const auto audio = capture.currentAudio(); (audio && audio != previousCapture)
                    || cursor + wholeSongObservationFrames > fixture.audio.getNumSamples())
                    return control::waitUntil (watchdog(), complete) || failed();
                remainingInBurst = 8; // Ample space in each 32-slot queue.
            }
            if (cursor + wholeSongObservationFrames > fixture.audio.getNumSamples())
            { remainingInBurst = 0; continue; }
            const auto due = origin + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
                std::chrono::duration<double> (static_cast<double> (suppliedFrames) / 48000.0));
            if (std::chrono::steady_clock::now() < due)
                std::this_thread::sleep_until (std::min (due, stalledAt));
            observeWholeSongFixture (controller, fixture, cursor, channels);
            suppliedFrames += wholeSongObservationFrames;
            --remainingInBurst;
        }
    }
};
}
