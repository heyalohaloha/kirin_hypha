#include "../src/local_blind/ExactRangeCapture.h"
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
    std::cout << "Exact range capture: PASS (14 bit-exact partitions, silence, 4 delays, 8 failures, 5 bounds, publication/retirement)\n";
}
