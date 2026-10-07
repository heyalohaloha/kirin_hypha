// Actual local non-RT completion through the C header; no IO role or filesystem access.
#include "kirin_hypha_ffi.h"
#include "kirin_hypha_time_snapshot_ffi.h"
#include "kirin_hypha_snapshot_types.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <exception>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
bool check (bool value, const char* description) {
    if (! value) std::fprintf (stderr, "TIME native probe failed: %s\n", description);
    return value;
}
bool sameSpan (const KirinTimeSourceSpanV2& a, const KirinTimeSourceSpanV2& b) {
    return a.epoch == b.epoch && a.incarnation == b.incarnation
        && a.generation == b.generation && a.token == b.token
        && a.sample_rate == b.sample_rate && a.channels == b.channels;
}
bool sameRawKey (const KirinTimeCurrentV2& a, const KirinTimeCurrentV2& b) {
    return sameSpan (a.span, b.span) && a.cutoff == b.cutoff && a.run == b.run
        && a.endpoint == b.endpoint && a.clock == b.clock;
}
void printNumberOrNull (double value) {
    if (std::isfinite (value)) std::printf ("%.3f", value);
    else std::fputs ("null", stdout);
}
}

bool runTimeSnapshotProbe() {
    try {
        const std::array<std::uint8_t, 1> roles { KIRIN_CHANNEL_ROLE_CENTRE };
        std::unique_ptr<KirinHypha, decltype (&kirin_hypha_destroy)> engine (
            kirin_hypha_create (48000, roles.data(), 1), kirin_hypha_destroy);
        if (! check (engine != nullptr, "mono engine")) return false;
        // Deliberately leave identity and IO role unassigned. Constructor, normal Watch
        // measurement and teardown use no plugin_data or Watch discovery in this state.
        KirinTimeSnapshotRequestV2 request { 2, sizeof (KirinTimeSnapshotRequestV2),
            sizeof (KirinTimeSnapshotV2), sizeof (KirinTimeHistoryEntryV2), 480000, KIRIN_TARGET_PRE, 0 };
        std::array<KirinTimeHistoryEntryV2, 1> main {}, psr {};
        KirinTimeSnapshotV2 first {}, second {}, last {};
        const auto poll = [&] (KirinTimeSnapshotV2& packet) {
            return kirin_hypha_poll_time_snapshot_v2 (engine.get(), &request,
                main.data(), 1, psr.data(), 1, &packet);
        };
        std::array<float, 480> samples {};
        constexpr double tau = 6.2831853071795864769;
        constexpr std::uint64_t suppliedFrames = 400u * 480u;
        const auto feedStarted = Clock::now();
        for (std::int64_t block = 0; block < 400; ++block) {
            for (std::size_t i = 0; i < samples.size(); ++i)
                samples[i] = static_cast<float> (0.2 * std::sin (tau * 1000.0
                    * static_cast<double> (block * 480 + static_cast<std::int64_t> (i)) / 48000.0));
            kirin_hypha_set_signal_state (engine.get(), KIRIN_SIGNAL_STATE_ACTIVE);
            kirin_hypha_note_capture_window (engine.get(), true, block * 480, 480,
                KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE, 0, false, 0, false, 0, 0, false, 0, false);
            if (! check (kirin_hypha_push_samples (engine.get(), samples.data(), samples.size(), 1),
                         "complete audio block admitted")) return false;
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
        const auto feedEnded = Clock::now();
        const auto deadline = feedEnded + std::chrono::milliseconds (250);
        bool ready = false;
        std::uint32_t pollCount = 0, busyCount = 0;
        std::uint8_t lastStatus = KIRIN_SNAPSHOT_BUSY;
        while (Clock::now() < deadline) {
            lastStatus = poll (first);
            ++pollCount;
            if (lastStatus == KIRIN_SNAPSHOT_BUSY) ++busyCount;
            // Finite readiness alone does not prove that the already-supplied tail is complete.
            // Wait for its exact final raw key within the unchanged 250 ms completion deadline.
            if (lastStatus == KIRIN_SNAPSHOT_SUCCESS && Clock::now() <= deadline
                && first.local_cutoff == suppliedFrames
                && first.main.current.cutoff == suppliedFrames
                && first.psr.current.cutoff == suppliedFrames
                && first.psr.current.endpoint == static_cast<std::int64_t> (suppliedFrames)
                && first.psr.current.run != 0
                && sameSpan (first.post_span, first.psr.current.span)
                && sameRawKey (first.main.current, first.psr.current)
                && first.psr.current.state == KIRIN_TIME_CURRENT_LIVE
                && (first.psr.current.finite_mask & (1u << 3)) != 0) { ready = true; break; }
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
        // Preserve arrival and last successful cutoff diagnostics even when completion fails.
        std::printf ("TIME_COMPLETION {\"supplied_frames\":%llu,\"ready\":%s,"
            "\"feed_wall_ms\":%.3f,\"observed_after_feed_ms\":%.3f,\"poll_count\":%u,"
            "\"busy_count\":%u,\"last_status\":%u,\"last_cutoff\":%llu,"
            "\"last_current_cutoff\":%llu,\"last_endpoint\":%lld,\"completion_age_ms\":",
            static_cast<unsigned long long> (suppliedFrames), ready ? "true" : "false",
            std::chrono::duration<double, std::milli> (feedEnded - feedStarted).count(),
            std::chrono::duration<double, std::milli> (Clock::now() - feedEnded).count(),
            pollCount, busyCount, static_cast<unsigned int> (lastStatus),
            static_cast<unsigned long long> (first.local_cutoff),
            static_cast<unsigned long long> (first.psr.current.cutoff),
            static_cast<long long> (first.psr.current.endpoint));
        printNumberOrNull (first.psr.current.completion_age_ms);
        std::fputs (",\"remaining_ms\":", stdout);
        printNumberOrNull (first.psr.current.remaining_ms);
        std::puts ("}");
        if (! check (ready, "final supplied raw slot completed within 250 ms")
            || ! check (first.psr.current.values[3] >= 2.7 && first.psr.current.values[3] <= 3.3,
                        "1 kHz mono sine PSR near 3 dB")) return false;
        // The completed final slot is immutable: no supplied tail remains to advance its key.
        // A later poll must consume its remaining lifetime instead of restarting the deadline.
        std::this_thread::sleep_for (std::chrono::milliseconds (25));
        if (! check (poll (second) == KIRIN_SNAPSHOT_SUCCESS, "second stable poll")
            || ! check (second.local_cutoff == suppliedFrames
                        && sameSpan (first.post_span, second.post_span)
                        && sameRawKey (first.main.current, second.main.current)
                        && sameRawKey (first.psr.current, second.psr.current),
                        "same final supplied raw slot")
            || ! check (second.psr.current.remaining_ms < first.psr.current.remaining_ms,
                        "poll does not refresh lifetime")
            || ! check (second.psr.current.completion_age_ms > first.psr.current.completion_age_ms,
                        "original completion age increases")
            || ! check (second.psr.current.remaining_ms + second.psr.current.completion_age_ms <= 400.001,
                        "original completion bounds deadline")) return false;
        std::printf ("TIME_PROBE {\"cutoff\":%llu,\"psr\":%.9f,\"age_first_ms\":%.3f,"
            "\"remaining_first_ms\":%.3f,\"age_second_ms\":%.3f,\"remaining_second_ms\":%.3f}\n",
            static_cast<unsigned long long> (second.local_cutoff), second.psr.current.values[3],
            first.psr.current.completion_age_ms, first.psr.current.remaining_ms,
            second.psr.current.completion_age_ms, second.psr.current.remaining_ms);
        std::this_thread::sleep_for (std::chrono::milliseconds (450));
        if (! check (poll (last) == KIRIN_SNAPSHOT_SUCCESS, "expired poll")
            || ! check (last.psr.current.finite_mask == 0, "expired finite values retired")
            || ! check (last.psr.current.state != KIRIN_TIME_CURRENT_LIVE, "expired state")) return false;
        const auto token = last.post_span.token;
        if (! check (kirin_hypha_reset_meter_session (engine.get()), "explicit session reset")
            || ! check (poll (last) == KIRIN_SNAPSHOT_SUCCESS, "reset poll")
            || ! check (last.post_span.token != token, "reset retires source span")
            || ! check (last.psr.current.finite_mask == 0, "reset cannot restore finite values")) return false;
        std::puts ("TIME original completion native probe: PASS");
        return true;
    } catch (const std::exception&) {
        std::fputs ("TIME native probe fixture setup failed\n", stderr);
        return false;
    }
}
