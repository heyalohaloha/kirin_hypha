#pragma once
#include "ReferenceControllerTestAccess.h"
#include "reference_whole_song_fixture.h"
#include <algorithm>
#include <thread>

namespace
{
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
        const auto& capture = control::captureFor (control::roleFor (controller, {1}), {});
        const auto watchdog = [watchdogSeconds] { return std::chrono::steady_clock::now() + std::chrono::seconds (watchdogSeconds); };
        if (! control::waitUntil (watchdog(), [&] {
            return complete() || control::captureEnabledFor (capture, {});
        })) return false;
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
                })) return false;
                stalledAt = watchdog(); // The preceding PCM has been consumed.
                if (complete()) return true;
                // Consumption precedes alignment and loudness measurement. Keep
                // this capture stable until that worker finishes, even on a slow host.
                // The watchdog detects a stall; it never declares success.
                if (const auto audio = capture.currentAudio(); (audio && audio != previousCapture)
                    || cursor + wholeSongObservationFrames > fixture.audio.getNumSamples())
                    return control::waitUntil (watchdog(), complete);
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
