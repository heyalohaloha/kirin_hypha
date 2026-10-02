#include "../../src/live_compare/LiveBlindSession.h"
#include "../../src/live_compare/LiveCompareSession.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

// Diagnostic only: the renderer plus the sampled Blind command/fault receipt. No clock reads
// are added to the product. Full processor, worker and DAW scheduling are deliberately excluded.
int main()
{
    using namespace hypha::live_compare;
    using Clock = std::chrono::steady_clock;
    auto ring = std::make_unique<Ring>();
    for (const int frames : { 64, 128, 256, 512 })
    for (const int mode : { 0, 1, 2, 3, 4, 5 }) // last two: normal loop / native-before-loop PPQ clamp
    {
        ring->initialise (123, 48000);
        ring->header.demand.store (1);
        PreFeeder feeder;
        PostRenderer renderer;
        PostLevel level;
        renderer.prepare (512, 48000);
        level.configure (48000);
        BlindSession blind;
        std::array<float, 512> left {}, right {};
        left.fill (0.1f); right.fill (0.1f);
        const float* input[] { left.data(), right.data() };
        float* output[] { left.data(), right.data() };
        std::vector<double> samples;
        samples.reserve (12000);
        unsigned rejected = 0;
        for (int i = 0; i < 12500; ++i)
        {
            BlockClock block;
            block.clock = block.project = static_cast<std::int64_t> (i) * frames;
            block.clockValid = block.projectValid = block.playing = true;
            block.frames = frames;
            if (mode >= 4 && i >= 400)
            {
                constexpr std::int64_t length = 24000;
                const auto origin = static_cast<std::int64_t> (400) * frames;
                block.project = origin + (block.clock - origin) % length;
                block.loop = { true, true, static_cast<double> (block.project) / 24000.0,
                    static_cast<double> (origin) / 24000.0,
                    static_cast<double> (origin + length) / 24000.0, 120.0 };
            }
            feeder.feed (*ring, block, input, 2);
            if (mode == 5)
            {
                block.clock += 1147688; // independent AU-like clock origin
                block.project -= 4096;  // known physical delay; native sample is not clamped
                if (block.loop.active)
                {
                    block.loop.ppq = static_cast<double> (block.project) / 24000.0;
                    // Delayed content before a newly enabled range still has linear PPQ.
                    // The observed AU clamp begins at the first actual wrap, not at LOOP ON.
                    if (static_cast<std::int64_t> (i - 400) * frames >= 24000)
                        block.loop.ppq = std::max (block.loop.start, block.loop.ppq);
                }
                if (block.project < 0) ++block.project;
            }
            if (mode >= 2) blind.start (true);
            if (mode == 3) block.afterGap = true;
            const auto start = Clock::now();
            const auto command = blind.command();
            const auto report = renderer.render (*ring, 123, 48000, block, output, 2,
                mode != 0, 1.0f, level, 1.0f, 1.0f, mode >= 2);
            if (command.active())
            {
                if (report.verdict != Verdict::accepted || report.guardTripped)
                {
                   #ifdef KIRIN_RECOVERY_BASELINE
                    blind.invalidate (command);
                   #else
                    blind.invalidate (command, report.reason);
                   #endif
                }
                else blind.observe (command, report.stableSource && report.gainSettled);
            }
            const double us = std::chrono::duration<double, std::micro> (Clock::now() - start).count();
            if (i >= 500)
            {
                samples.push_back (us);
                if (report.verdict != Verdict::accepted) ++rejected;
            }
        }
        std::sort (samples.begin(), samples.end());
        std::printf ("frames=%d mode=%d median_us=%.3f p99_us=%.3f rejected=%u\n", frames, mode,
                     samples[samples.size() / 2], samples[samples.size() * 99 / 100], rejected);
        if (mode >= 4 && rejected != 0) return 1; // fast rejection is not an audible-path budget
    }
}
