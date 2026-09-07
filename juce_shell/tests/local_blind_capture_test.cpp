#include "../src/local_blind/PairCaptureBarrier.h"
#include "../src/local_blind/ExactRangeCaptureSlot.h"
#include "../src/local_blind/LocalBlindCaptureLane.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

using namespace hypha::local_blind;
static void require (bool value) { if (! value) std::abort(); }

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
    // The caller supplies separately proven native-coordinate ranges, applying a delay once.
    // This fixture proves the capture primitive; it does not prove a DAW's PDC metadata.
    for (int delay : { 0, 1, 31, 8192 })
    {
        ExactRangeCapture pre ({ 2, 48000, 1, 0, 12 }, 48);
        ExactRangeCapture post ({ 2, 48000, 1, delay, 12 }, 48);
        const float* pointers[] = { input.data() };
        pre.push (pointers, 1, 12, 0, 2, true, 48000);
        post.push (pointers, 1, 12, delay, 2, true, 48000);
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
    // One name-independent exact pair and capture generation bind both native ranges.
    {
        const ExactPairBinding pair { 11, "project-a", "pre-unnamed" };
        const ExactCaptureRequest request {
            "12345678-1234-4234-8234-123456789abc", pair, 22, 33, 48000, 1,
            0, 31, 12, 1000
        };
        PairCaptureBarrier barrier (request);
        ExactRangeCapture pre (barrier.range (CaptureSide::pre), 48);
        ExactRangeCapture post (barrier.range (CaptureSide::post), 48);
        const float* pointers[] = { input.data() };
        pre.push (pointers, 1, 12, 0, 22, true, 48000);
        post.push (pointers, 1, 12, 31, 22, true, 48000);
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
    for (int variant = 0; variant < 7; ++variant)
    {
        ExactCaptureRequest request {
            "12345678-1234-4234-8234-123456789abc",
            { 11, "project-a", "pre-a" }, 22, 33, 48000, 1, 0, 31, 12, 1000
        };
        if (variant == 0) request.requestId = "short";
        if (variant == 1) request.requestId = std::string (36, 'x');
        if (variant == 2) request.captureGeneration = 0;
        if (variant == 3) request.clockGeneration = 0;
        if (variant == 4) request.frames = 0;
        if (variant == 5) request.frames = 192001;
        if (variant == 6) request.expiresAtUnixMs = 0;
        bool refused = false;
        try { PairCaptureBarrier barrier (request); }
        catch (const std::invalid_argument&) { refused = true; }
        require (refused);
    }
    // A pair transition invalidates an unfinished request even when the human label is unchanged.
    {
        PairCaptureBarrier barrier ({ 11, "project-a", "pre-a" }, 22, 33, 48000, 2, 0, 0, 12);
        barrier.invalidateIfPairChanged ({ 12, "project-a", "pre-a" });
        require (barrier.state() == PairCaptureState::invalid);
        require (barrier.failure() == PairCaptureFailure::stalePair);
    }
    for (int variant = 0; variant < 8; ++variant)
    {
        const ExactPairBinding pair { 11, "project-a", "pre-a" };
        PairCaptureBarrier barrier (pair, 22, 33, 48000, 1, 0, 31, 12);
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
    for (int variant = 0; variant < 4; ++variant)
    {
        ExactPairBinding pair { 11, "project-a", "pre-a" };
        auto generation = std::uint64_t { 22 };
        auto clock = std::uint64_t { 33 };
        auto frames = std::int64_t { 12 };
        if (variant == 0) pair.preInstanceId.clear();
        if (variant == 1) generation = 0;
        if (variant == 2) frames = 0;
        if (variant == 3) clock = 0;
        bool refused = false;
        try { PairCaptureBarrier barrier (pair, generation, clock, 48000, 1, 0, 0, frames); }
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
            CaptureRange { 44, 48000, 1, 0, 12 }, 48)));
        require (! slot.publish (std::make_unique<ExactRangeCapture> (
            CaptureRange { 45, 48000, 1, 0, 12 }, 48)));
        const float* pointers[] = { input.data() };
        require (slot.push (pointers, 1, 12, 0, 44, true, 48000));
        require (slot.control() != nullptr && slot.control()->completedPcm() != nullptr);
        require (slot.retireCompleted());
        require (! slot.hasPublishedRealtime() && ! slot.hasStorage());
        require (! slot.push (pointers, 1, 12, 0, 44, true, 48000));

        require (slot.publish (std::make_unique<ExactRangeCapture> (
            CaptureRange { 46, 48000, 1, 0, 12 }, 48)));
        require (slot.cancelAndRetire());
        require (! slot.hasPublishedRealtime() && ! slot.hasStorage());
    }
    // The role-local lane binds request expiry, prepared format and the correct PRE/POST range.
    {
        const ExactCaptureRequest request {
            "12345678-1234-4234-8234-123456789abc",
            { 51, "project-a", "pre-unnamed" }, 52, 53, 48000, 1,
            0, 31, 12, 2000
        };
        LocalBlindCaptureLane preLane (CaptureSide::pre);
        LocalBlindCaptureLane postLane (CaptureSide::post);
        require (! preLane.arm (request, 44100, 1, 1000, 48));
        require (! preLane.arm (request, 48000, 1, 2001, 48));
        require (preLane.arm (request, 48000, 1, 1000, 48));
        require (! preLane.arm (request, 48000, 1, 1000, 48));
        const float* pointers[] = { input.data() };
        require (preLane.process (pointers, 1, 12, 0, true, true, false, true, 48000));
        CaptureReceipt receipt;
        require (preLane.receipt (receipt));
        require (receipt.side == CaptureSide::pre && receipt.range.start == 0);
        require (receipt.state == CaptureState::complete);
        require (preLane.process (pointers, 1, 12, 12, false, false, true, false, 44100));
        require (preLane.receipt (receipt) && receipt.state == CaptureState::complete);
        require (preLane.completedCapture() != nullptr);
        require (preLane.retireFinal() && ! preLane.hasActiveRequest());

        require (postLane.arm (request, 48000, 1, 1000, 48));
        require (postLane.process (pointers, 1, 12, 31, true, true, false, true, 48000));
        require (postLane.receipt (receipt));
        require (receipt.side == CaptureSide::post && receipt.range.start == 31);
        require (receipt.state == CaptureState::complete && postLane.retireFinal());

        for (int variant = 0; variant < 4; ++variant)
        {
            require (preLane.arm (request, 48000, 1, 1000, 48));
            const bool positionValid = variant != 0;
            const bool timelineActive = variant != 1;
            const bool bypassed = variant == 2;
            const bool realtime = variant != 3;
            require (preLane.process (pointers, 1, 12, 0, positionValid, timelineActive,
                                      bypassed, realtime, 48000));
            require (preLane.receipt (receipt));
            require (receipt.state == CaptureState::invalid);
            require (receipt.failure == (variant == 3 ? CaptureFailure::nonRealtime
                                                      : CaptureFailure::transport));
            require (preLane.retireFinal());
        }
    }
    std::cout << "Exact range capture: PASS (ranges, pair barrier, role lane, bounds, retirement)\n";
}
