#pragma once
#include "reference_whole_song_fixture.h"
#include "ReferenceControllerTestAccess.h"
#include <algorithm>
#include <thread>

namespace
{
// One test thread owns PCM and transport, including intentional seeks between
// waits. Scheduler delays never become an extra delay for every audio block.
class WholeSongInputClock
{
public:
    explicit WholeSongInputClock (hypha::tests::reference_control::Deadline end)
        : deadline (end), origin (std::chrono::steady_clock::now()) {}

    bool expired() const { return std::chrono::steady_clock::now() >= deadline; }

    bool observeNext (ref::ReferenceComparisonController& controller,
                      const WholeSongFixture& fixture, int& cursor)
    {
        if (expired()) return false;
        if (remainingInBurst == 0)
        {
            // Leave ample space in every 32-slot queue. A stalled consumer
            // fails at the same deadline rather than dropping capture PCM.
            if (! hypha::tests::reference_control::waitUntil (deadline, [&] {
                for (int role = 1; role <= 3; ++role)
                    if (! hypha::tests::reference_control::captureFor (
                            hypha::tests::reference_control::roleFor (controller, { role }), {})
                            .observationQueueDrained()) return false;
                return true;
            })) return false;
            remainingInBurst = 8;
        }
        const auto due = origin + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
            std::chrono::duration<double> (static_cast<double> (suppliedFrames) / 48000.0));
        if (std::chrono::steady_clock::now() < due)
            std::this_thread::sleep_until (std::min (due, deadline));
        if (expired()) return false;
        observeWholeSongFixture (controller, fixture, cursor);
        suppliedFrames += wholeSongObservationFrames;
        --remainingInBurst;
        return ! expired();
    }

private:
    const hypha::tests::reference_control::Deadline deadline, origin;
    std::int64_t suppliedFrames = 0;
    int remainingInBurst = 0;
};
}
