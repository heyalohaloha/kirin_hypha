#pragma once
#include "kirin_hypha_navigation_snapshot_ffi.h"
#include <cmath>
#include <cstdint>
#include <iterator>
#include <algorithm>

namespace hypha::session_presentation
{
// Presentation policy only. The caller owns the whole frame/history/chain/Watch/
// coverage packet. retain never authorises attaching old coverage to a new frame.
enum class Decision { accept, retain, invalidate };
struct Input
{
    const KirinObservatoryFrame* frame = nullptr;
    const KirinMeterSessionV2* coverage = nullptr;
    unsigned coverageStatus = KIRIN_SNAPSHOT_BUSY;
    bool packetReady = true;
};
class Gate
{
public:
    // Call before issuing RESET, not when merely opening its confirmation dialog.
    void invalidate() noexcept { armed = false; }
    bool retainingEligible() const noexcept { return armed; }
    Decision decide (const Input& input, double nowMs) noexcept
    {
        const auto* frame = input.frame;
        if (!std::isfinite(nowMs) || frame == nullptr
            || frame->version != KIRIN_OBSERVATORY_FRAME_VERSION)
        { invalidate(); return Decision::invalidate; }
        const auto& meter = frame->meter;
        const bool active = frame->signal_state == KIRIN_SIGNAL_STATE_ACTIVE
            && meter.state == KIRIN_METER_SESSION_ACTIVE && frame->meter_stalled == 0;
        const bool same = sameIdentity(meter, accepted);
        if (haveAccepted && same && (meter.active_frames < accepted.active_frames
            || meter.observed_frames < accepted.observed_frames))
        { invalidate(); return Decision::invalidate; }
        const bool observedReversal = haveObserved && sameIdentity(meter, observed)
            && (meter.active_frames < observed.active_frames || meter.observed_frames < observed.observed_frames);
        observed = meter; haveObserved = true;
        if (observedReversal) { invalidate(); return Decision::invalidate; }
        const bool forward = same && frame->comparison_identity == acceptedBinding
            && frame->keep_first_pass_only == acceptedKeepFirstPass
            && frame->comparison_state == acceptedComparisonState
            && frame->comparison_reason == acceptedComparisonReason
            && frame->delta_available == acceptedDeltaAvailable
            && meter.active_frames >= accepted.active_frames
            && meter.observed_frames >= accepted.observed_frames;
        if (input.coverageStatus == KIRIN_SNAPSHOT_BUSY)
        {
            if (armed && active && forward && nowMs >= acceptedAt && nowMs - acceptedAt < 1000.0)
                return Decision::retain;
            invalidate();
            return Decision::invalidate;
        }
        // A successful pending, invalid, older or different-session coverage is
        // not a transient BUSY. The caller exposes its normal unconfirmed state.
        const auto* coverage = input.coverage;
        if (!input.packetReady) { invalidate(); return Decision::invalidate; }
        if (input.coverageStatus != KIRIN_SNAPSHOT_SUCCESS || coverage == nullptr
            || coverage->version != 2 || coverage->struct_size != sizeof(KirinMeterSessionV2)
            || coverage->summary_status > KIRIN_SESSION_SUMMARY_PENDING_TAIL
            || !sameIdentity(meter, coverage->session)
            || coverage->session.active_frames < meter.active_frames
            || coverage->session.observed_frames < meter.observed_frames)
        { invalidate(); return Decision::invalidate; }
        // The coverage may legitimately include a newer, completed 10 ms tail.
        // Neither compare it for equality with the 100 ms frame nor fabricate PLR.
        const bool complete = coverage->summary_status == KIRIN_SESSION_SUMMARY_COMPLETE;
        armed = active && complete && coverage->session.state == KIRIN_METER_SESSION_ACTIVE;
        haveAccepted = true;
        accepted = meter;
        acceptedBinding = frame->comparison_identity;
        acceptedKeepFirstPass = frame->keep_first_pass_only;
        acceptedComparisonState = frame->comparison_state;
        acceptedComparisonReason = frame->comparison_reason;
        acceptedDeltaAvailable = frame->delta_available;
        acceptedAt = nowMs;
        return Decision::accept;
    }
private:
    static bool sameIdentity (const KirinMeterSession& a, const KirinMeterSession& b) noexcept
    {
        return a.measurement_epoch == b.measurement_epoch && a.generation == b.generation
            && a.sample_rate == b.sample_rate && a.channels == b.channels && a.layout_id == b.layout_id
            && std::equal(std::begin(a.channel_positions), std::end(a.channel_positions),
                          std::begin(b.channel_positions));
    }
    KirinMeterSession accepted {}, observed {};
    std::uint64_t acceptedBinding = 0;
    unsigned acceptedKeepFirstPass = 0, acceptedComparisonState = 0;
    unsigned acceptedComparisonReason = 0, acceptedDeltaAvailable = 0;
    double acceptedAt = 0;
    bool armed = false, haveAccepted = false, haveObserved = false;
};
}
