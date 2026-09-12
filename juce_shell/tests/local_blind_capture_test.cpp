#include "../src/local_blind/PairCaptureBarrier.h"
#include "../src/local_blind/ExactRangeCaptureSlot.h"
#include "../src/local_blind/LocalBlindCaptureLane.h"
#include "../src/local_blind/LocalBlindCaptureOwner.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

using namespace hypha::local_blind;
static void require (bool value) { if (! value) std::abort(); }

static CaptureClockObservation clockAt (std::int64_t position, int frames)
{
    return { position, frames, 1, 1, 0, 96, true, true, false, true, false, true };
}

int main()
{
    // Exact native ranges include negative host positions, silence, and a partial last callback.
    std::array<float, 12> input { 0, 0, 0, 0.25f, 0.5f, 0, -0.5f, 0, 0.1f, 0, 0, 0 };
    std::array<float, 12> right { 0, 0, -0.0f, -0.75f, 0, 0.125f, 0, 0.5f, -0.1f, 0, 0, 0 };
    for (int channels : { 1, 2 })
        for (int block : { 1, 2, 3, 4, 5, 7, 12 })
        {
            ExactRangeCapture capture ({ 1, 48000, channels, -3, 8 }, 8 * channels * sizeof (float));
            require (capture.completedPcm() == nullptr);
            for (int start = 0; start < 12; start += block)
            {
                const float* pointers[] = { input.data() + start, right.data() + start };
                capture.push (pointers, channels, std::min (block, 12 - start), start - 5, 1, true, 48000);
            }
            require (capture.state() == CaptureState::complete);
            const auto& pcm = *capture.completedPcm();
            for (int f = 0; f < 8; ++f)
                for (int c = 0; c < channels; ++c)
                {
                    const auto& source = c == 0 ? input : right;
                    require (std::memcmp (&pcm[f * channels + c], &source[f + 2], sizeof (float)) == 0);
                }
            require (capture.allocatedBytes() == 8u * channels * sizeof (float));
        }
    // A pair request uses one host-native range. A host-specific PDC offset is never injected.
    for (int start : { -8192, -1, 0, 8192 })
    {
        ExactRangeCapture pre ({ 2, 48000, 1, start, 12 }, 48);
        ExactRangeCapture post ({ 2, 48000, 1, start, 12 }, 48);
        const float* pointers[] = { input.data() };
        pre.push (pointers, 1, 12, start, 2, true, 48000);
        post.push (pointers, 1, 12, start, 2, true, 48000);
        require (*pre.completedPcm() == *post.completedPcm());
    }
    // An all-zero range is complete data, not an empty/non-silent capture failure.
    {
        std::array<float, 12> zero {};
        const float* pointers[] = { zero.data() };
        ExactRangeCapture silence ({ 4, 48000, 1, 0, 12 }, 48);
        silence.push (pointers, 1, 12, 0, 4, true, 48000);
        require (silence.completedPcm() && silence.completedPcm()->size() == 12);
        require (std::memcmp (silence.completedPcm()->data(), zero.data(), 48) == 0);
    }
    for (int variant = 0; variant < 8; ++variant)
    {
        ExactRangeCapture capture ({ 3, 48000, 1, 0, 12 }, 48);
        auto pcm = input;
        const float* pointers[] = { pcm.data() };
        capture.push (pointers, 1, 3, 0, 3, true, 48000);
        if (variant == 0) capture.push (pointers, 1, 3, 4, 3, true, 48000); // gap
        if (variant == 1) capture.push (pointers, 1, 3, 2, 3, true, 48000); // overlap/seek
        if (variant == 2) capture.push (pointers, 1, 3, 3, 4, true, 48000); // new generation
        if (variant == 3) capture.push (pointers, 1, 3, 3, 3, false, 48000); // offline
        if (variant == 4) capture.push (pointers, 2, 3, 3, 3, true, 48000); // layout
        if (variant == 5) {
            pcm[0] = std::numeric_limits<float>::quiet_NaN();
            capture.push (pointers, 1, 3, 3, 3, true, 48000);
        }
        if (variant == 6) capture.cancel();
        if (variant == 7) capture.push (pointers, 1, 3, 3, 3, true, 44100);
        require (capture.state() == CaptureState::invalid && capture.completedPcm() == nullptr);
        capture.push (pointers, 1, 12, 0, 3, true, 48000);
        require (capture.completedPcm() == nullptr); // invalid does not re-arm itself
    }
    for (int variant = 0; variant < 5; ++variant)
    {
        CaptureRange range { 1, 48000, 1, 0, 12 };
        if (variant == 0) range.generation = 0;
        if (variant == 1) range.channels = 3;
        if (variant == 2) range.start = std::numeric_limits<std::int64_t>::max() - 1;
        if (variant == 3) range.frames = -1;
        bool refused = false;
        try { ExactRangeCapture capture (range, variant == 4 ? 47 : 48); }
        catch (const std::invalid_argument&) { refused = true; }
        require (refused);
    }
    // One name-independent exact pair and capture generation bind one shared native range.
    {
        const ExactPairBinding pair { 11, "project-a", "pre-unnamed" };
        const ExactCaptureRequest request {
            "12345678-1234-4234-8234-123456789abc", pair, 22, 33, 1, 0, 48000, 1,
            0, 12, 1000
        };
        PairCaptureBarrier barrier (request);
        ExactRangeCapture pre (barrier.range (CaptureSide::pre), 48);
        ExactRangeCapture post (barrier.range (CaptureSide::post), 48);
        const float* pointers[] = { input.data() };
        pre.push (pointers, 1, 12, 0, 22, true, 48000);
        post.push (pointers, 1, 12, 0, 22, true, 48000);
        require (barrier.accept ({ pair, 33, CaptureSide::pre, pre.range(), pre.state(), pre.failure() }));
        require (barrier.state() == PairCaptureState::pending);
        require (barrier.accept ({ pair, 33, CaptureSide::post, post.range(), post.state(), post.failure() }));
        require (barrier.state() == PairCaptureState::complete);
        require (*pre.completedPcm() == *post.completedPcm());
        barrier.invalidateIfPairChanged ({ 12, "project-a", "pre-unnamed" });
        require (barrier.state() == PairCaptureState::invalid);
        require (barrier.failure() == PairCaptureFailure::stalePair);
    }
    // The C ABI envelope is rejected before it can authorize a capture barrier.
    for (int variant = 0; variant < 9; ++variant)
    {
        ExactCaptureRequest request {
            "12345678-1234-4234-8234-123456789abc",
            { 11, "project-a", "pre-a" }, 22, 33, 1, 0, 48000, 1, 0, 12, 1000
        };
        if (variant == 0) request.requestId = "short";
        if (variant == 1) request.requestId = std::string (36, 'x');
        if (variant == 2) request.captureGeneration = 0;
        if (variant == 3) request.clockGeneration = 0;
        if (variant == 4) request.clockSource = 0;
        if (variant == 5) request.clockPositionAtIssue = 1;
        if (variant == 6) request.frames = 0;
        if (variant == 7) request.frames = 192001;
        if (variant == 8) request.expiresAtUnixMs = 0;
        bool refused = false;
        try { PairCaptureBarrier barrier (request); }
        catch (const std::invalid_argument&) { refused = true; }
        require (refused);
    }
    // A pair transition invalidates an unfinished request even when the human label is unchanged.
    {
        PairCaptureBarrier barrier ({ 11, "project-a", "pre-a" }, 22, 33, 1, 48000, 2, 0, 12);
        barrier.invalidateIfPairChanged ({ 12, "project-a", "pre-a" });
        require (barrier.state() == PairCaptureState::invalid);
        require (barrier.failure() == PairCaptureFailure::stalePair);
    }
    for (int variant = 0; variant < 8; ++variant)
    {
        const ExactPairBinding pair { 11, "project-a", "pre-a" };
        PairCaptureBarrier barrier (pair, 22, 33, 1, 48000, 1, 0, 12);
        CaptureReceipt receipt { pair, 33, CaptureSide::pre, barrier.range (CaptureSide::pre),
                                 CaptureState::complete, CaptureFailure::none };
        if (variant == 0) receipt.pair.generation++;
        if (variant == 1) receipt.pair.projectHash = "project-b";
        if (variant == 2) receipt.pair.preInstanceId = "pre-b";
        if (variant == 3) receipt.clockGeneration++;
        if (variant == 4) receipt.range.generation++;
        if (variant == 5) receipt.range.start++;
        if (variant == 6) receipt.state = CaptureState::pending;
        if (variant == 7) receipt.failure = CaptureFailure::format;
        require (! barrier.accept (receipt));
        require (barrier.state() == PairCaptureState::invalid);
        require (barrier.failure() == PairCaptureFailure::receipt);
    }
    for (int variant = 0; variant < 5; ++variant)
    {
        ExactPairBinding pair { 11, "project-a", "pre-a" };
        auto generation = std::uint64_t { 22 };
        auto clock = std::uint64_t { 33 };
        auto source = std::uint8_t { 1 };
        auto frames = std::int64_t { 12 };
        if (variant == 0) pair.preInstanceId.clear();
        if (variant == 1) generation = 0;
        if (variant == 2) frames = 0;
        if (variant == 3) clock = 0;
        if (variant == 4) source = 0;
        bool refused = false;
        try { PairCaptureBarrier barrier (pair, generation, clock, source, 48000, 1, 0, frames); }
        catch (const std::invalid_argument&) { refused = true; }
        require (refused);
    }
    // Publication is immutable. Worker-side cancellation never frees producer-owned storage.
    ExactRangeCapture capture ({ 1, 48000, 1, 0, 12 }, 48);
    std::thread producer ([&] {
        const float* pointers[] = { input.data() };
        capture.push (pointers, 1, 12, 0, 1, true, 48000);
    });
    while (capture.state() == CaptureState::pending) std::this_thread::yield();
    require (capture.completedPcm() && capture.completedPcm()->size() == input.size());
    require (std::memcmp (capture.completedPcm()->data(), input.data(), 48) == 0);
    producer.join(); // storage retirement, independently of PCM publication
    capture.cancel();
    require (capture.completedPcm() == nullptr);
    // A role-local publication slot keeps allocation and reclamation off the Audio Thread.
    {
        ExactRangeCaptureSlot slot;
        require (slot.publish (std::make_unique<ExactRangeCapture> (
            CaptureRange { 44, 48000, 1, 0, 12 }, 48), 1, 0, 0));
        require (! slot.publish (std::make_unique<ExactRangeCapture> (
            CaptureRange { 45, 48000, 1, 1, 12 }, 48), 1, 0, 0));
        require (! slot.publish (std::make_unique<ExactRangeCapture> (
            CaptureRange { 45, 48000, 1, 0, 12 }, 48), 1, 1, 0));
        const float* pointers[] = { input.data() };
        require (slot.process (pointers, 1, clockAt (0, 12), 48000));
        require (slot.control() != nullptr && slot.control()->completedPcm() != nullptr);
        require (slot.retireCompleted());
        require (! slot.hasPublishedRealtime() && ! slot.hasStorage());
        require (! slot.process (pointers, 1, clockAt (0, 12), 48000));

        require (slot.publish (std::make_unique<ExactRangeCapture> (
            CaptureRange { 46, 48000, 1, 0, 12 }, 48), 1, 0, 0));
        require (slot.cancelAndRetire());
        require (! slot.hasPublishedRealtime() && ! slot.hasStorage());
    }
    // The role-local lane binds request expiry, prepared format and the correct PRE/POST range.
    {
        const ExactCaptureRequest request {
            "12345678-1234-4234-8234-123456789abc",
            { 51, "project-a", "pre-unnamed" }, 52, 53, 1, 0, 48000, 1,
            0, 12, 2000
        };
        LocalBlindCaptureLane preLane (CaptureSide::pre);
        LocalBlindCaptureLane postLane (CaptureSide::post);
        require (! preLane.arm (request, 44100, 1, 1000, 48));
        require (! preLane.arm (request, 48000, 1, 2001, 48));
        require (preLane.arm (request, 48000, 1, 1000, 48));
        require (! preLane.arm (request, 48000, 1, 1000, 48));
        const float* pointers[] = { input.data() };
        require (preLane.process (pointers, 1, clockAt (0, 12), 48000));
        CaptureReceipt receipt;
        require (preLane.receipt (receipt));
        require (receipt.side == CaptureSide::pre && receipt.range.start == 0);
        require (receipt.state == CaptureState::complete);
        auto ignoredAfterComplete = clockAt (12, 12);
        ignoredAfterComplete.positionValid = ignoredAfterComplete.timelineActive = false;
        ignoredAfterComplete.bypassed = true;
        ignoredAfterComplete.realtime = false;
        require (preLane.process (pointers, 1, ignoredAfterComplete, 44100));
        require (preLane.receipt (receipt) && receipt.state == CaptureState::complete);
        require (preLane.completedCapture() != nullptr);
        require (preLane.retireFinal() && ! preLane.hasActiveRequest());

        require (postLane.arm (request, 48000, 1, 1000, 48));
        require (postLane.process (pointers, 1, clockAt (0, 12), 48000));
        require (postLane.receipt (receipt));
        require (receipt.side == CaptureSide::post && receipt.range.start == 0);
        require (receipt.state == CaptureState::complete && postLane.retireFinal());

        for (int variant = 0; variant < 4; ++variant)
        {
            require (preLane.arm (request, 48000, 1, 1000, 48));
            const bool positionValid = variant != 0;
            const bool timelineActive = variant != 1;
            const bool bypassed = variant == 2;
            const bool realtime = variant != 3;
            auto invalidClock = clockAt (0, 12);
            invalidClock.positionValid = positionValid;
            invalidClock.timelineActive = timelineActive;
            invalidClock.bypassed = bypassed;
            invalidClock.realtime = realtime;
            require (preLane.process (pointers, 1, invalidClock, 48000));
            require (preLane.receipt (receipt));
            require (receipt.state == CaptureState::invalid);
            require (receipt.failure == (variant == 3 ? CaptureFailure::nonRealtime
                                                      : CaptureFailure::transport));
            require (preLane.retireFinal());
        }

        // Once armed, the role's own callbacks must stay on one continuous clock and keep the
        // same optional presentation facts. These values detect change; they never shift PCM.
        auto guarded = request;
        guarded.nativeStart = 12;
        for (int variant = 0; variant < 5; ++variant)
        {
            require (preLane.arm (guarded, 48000, 1, 1000, 48));
            require (preLane.process (pointers, 1, clockAt (0, 6), 48000));
            auto changedClock = clockAt (6, 6);
            if (variant == 0) changedClock.position = 0;          // seek or loop wrap
            if (variant == 1) changedClock.source = 2;
            if (variant == 2) changedClock.outputLatency = 97;
            if (variant == 3) changedClock.hasOutputLatency = false;
            if (variant == 4) changedClock.presentationSource = 2;
            require (preLane.process (pointers, 1, changedClock, 48000));
            require (preLane.receipt (receipt) && receipt.state == CaptureState::invalid);
            require (receipt.failure == CaptureFailure::clock);
            require (preLane.retireFinal());
        }
        require (preLane.arm (guarded, 48000, 1, 1000, 48));
        require (preLane.process (pointers, 1, clockAt (13, 6), 48000));
        require (preLane.receipt (receipt) && receipt.failure == CaptureFailure::clock);
        require (preLane.retireFinal());
        auto rewoundBeforeFirstCallback = guarded;
        rewoundBeforeFirstCallback.clockPositionAtIssue = 6;
        require (preLane.arm (rewoundBeforeFirstCallback, 48000, 1, 1000, 48));
        require (preLane.process (pointers, 1, clockAt (0, 6), 48000));
        require (preLane.receipt (receipt) && receipt.failure == CaptureFailure::clock);
        require (preLane.retireFinal());
    }
    // The non-RT owner installs PRE before acknowledging it and installs POST only after the
    // matching peer response. One exact request remains the authority through completion.
    {
        const ExactCaptureRequest request {
            "12345678-1234-4234-8234-123456789abc",
            { 51, "project-a", "pre-unnamed" }, 52, 53, 1, 0, 48000, 1,
            0, 12, 2000
        };
        LocalBlindCaptureOwner pre (CaptureSide::pre);
        require (pre.beginPre (request, 48000, 1, 1000));
        require (pre.view().phase == CaptureOwnerPhase::capturing);
        pre.confirmPreAcknowledgement (false);
        require (pre.view().phase == CaptureOwnerPhase::failed);
        require (pre.view().failure == CaptureOwnerFailure::peerRejected);
        pre.reset();

        require (pre.beginPre (request, 48000, 1, 1000));
        pre.confirmPreAcknowledgement (true);
        const float* pointers[] = { input.data() };
        require (pre.process (pointers, 1, clockAt (0, 12), 48000));
        // Completion on the exact audio timeline wins even when the non-RT owner observes it
        // after the admission lease. Finalization must not discard already-complete PCM.
        pre.servicePre (&request, 2001);
        require (pre.view().phase == CaptureOwnerPhase::complete);
        require (pre.completedCapture() != nullptr);
        auto changed = request;
        changed.pair.generation++;
        require (changed != request);
        pre.servicePre (&changed, 2002);
        require (pre.view().phase == CaptureOwnerPhase::failed);
        require (pre.view().failure == CaptureOwnerFailure::stalePair);
        require (pre.completedCapture() == nullptr);
        pre.reset();

        require (pre.beginPre (request, 48000, 1, 1000));
        pre.confirmPreAcknowledgement (true);
        pre.servicePre (&request, 2001);
        require (pre.view().phase == CaptureOwnerPhase::failed);
        require (pre.view().failure == CaptureOwnerFailure::expired);
        pre.reset();

        LocalBlindCaptureOwner post (CaptureSide::post);
        require (post.beginPost (request));
        const auto pair = request.pair;
        post.servicePost (false, &pair, 48000, 1, 1000);
        require (post.view().phase == CaptureOwnerPhase::awaitingPeer);
        auto wrongPair = pair;
        wrongPair.preInstanceId = "another-pre";
        post.servicePost (true, &wrongPair, 48000, 1, 1001);
        require (post.view().phase == CaptureOwnerPhase::failed);
        require (post.view().failure == CaptureOwnerFailure::stalePair);
        post.reset();

        require (post.beginPost (request));
        post.servicePost (true, &pair, 48000, 1, 1000);
        require (post.view().phase == CaptureOwnerPhase::capturing);
        require (post.process (pointers, 1, clockAt (0, 12), 48000));
        post.servicePost (false, &pair, 48000, 1, 2001);
        require (post.view().phase == CaptureOwnerPhase::complete);
        CaptureReceipt receipt;
        require (post.receipt (receipt));
        require (receipt.side == CaptureSide::post && receipt.range.start == 0);
        require (post.view().failure == CaptureOwnerFailure::none);
        post.servicePost (false, &wrongPair, 48000, 1, 2002);
        require (post.view().phase == CaptureOwnerPhase::failed);
        require (post.view().failure == CaptureOwnerFailure::stalePair);
        post.reset();

        require (post.beginPost (request));
        post.servicePost (true, &pair, 48000, 1, 2001);
        require (post.view().failure == CaptureOwnerFailure::expired);
        post.reset();

        require (post.beginPost (request));
        post.servicePost (true, &pair, 48000, 1, 1000);
        require (post.process (pointers, 1, clockAt (0, 6), 48000));
        post.servicePost (false, &pair, 48000, 1, 2001);
        require (post.view().phase == CaptureOwnerPhase::failed);
        require (post.view().failure == CaptureOwnerFailure::expired);
        post.reset();
        require (post.beginPost (request));
        post.servicePost (true, &pair, 44100, 1, 1000);
        require (post.view().failure == CaptureOwnerFailure::armRejected);
        post.reset();

        require (post.beginPost (request));
        post.servicePost (true, &pair, 48000, 1, 1000);
        require (post.process (pointers, 1, clockAt (0, 6), 48000));
        require (post.process (pointers, 1, clockAt (7, 5), 48000));
        post.servicePost (true, &pair, 48000, 1, 1001);
        require (post.view().phase == CaptureOwnerPhase::failed);
        require (post.view().failure == CaptureOwnerFailure::captureFailed);
        // The clock guard rejects the transport gap before PCM ingress can classify it as a
        // range discontinuity; preserve that exact producer-side reason across retirement.
        require (post.view().captureFailure == CaptureFailure::clock);
        post.reset();
        require (post.view().captureFailure == CaptureFailure::none);
    }
    std::cout << "Exact range capture: PASS (ranges, pair barrier, role lane, bounds, retirement)\n";
}
