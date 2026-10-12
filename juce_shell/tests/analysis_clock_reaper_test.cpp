#include "../src/live_compare/CertifiedProjectAnalysisClock.h"
#include "pdc_validation_delay/FixedValidationDelay.h"
#undef NDEBUG
#include <array>
#include <cassert>
#include <iostream>
using namespace hypha::live_compare;

int main()
{
    constexpr int frames = 256, delay = 4096, rate = 96000, length = 96 * frames;
    for (bool loop : { false, true })
    {
        CertifiedProjectAnalysisClock preClock, postClock;
        ContinuousClock auditionClock;
        hypha::pdc_validation::FixedValidationDelay line; line.prepare (1);
        int measured = 0, wraps = 0;
        for (int callback = 0; callback < 500; ++callback)
        {
            BlockClock pre;
            pre.frames = frames; pre.playing = pre.projectValid = true;
            pre.clockBasis = static_cast<std::uint8_t> (ClockBasis::vst3Continuous);
            pre.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::certifiedProject);
            pre.presentationSource = 1; // neither continuousTimeSamples nor output latency
            const auto position = callback * frames;
            pre.project = loop ? positiveModulo (position, length) : position;
            if (loop) pre.loop = { true, true, pre.project / 48000.0, 0, length / 48000.0, 120 };
            auto post = pre;
            post.project = loop ? positiveModulo (position - delay, length) : position - delay;
            post.loop.ppq = post.project / 48000.0;
            std::array<float, frames> audio {};
            for (int i = 0; i < frames; ++i)
                audio[static_cast<std::size_t> (i)] = static_cast<float> (loop ? (position + i) % length : position + i);
            auto* ptr = audio.data(); assert (line.process (&ptr, 1, frames));
            const auto preReading = preClock.observe (pre, rate, true);
            const auto reading = postClock.observe (post, rate, true);
            if (reading.valid && callback >= delay / frames)
            {
                ++measured;
                for (int i = 0; i < frames; ++i)
                    assert (audio[static_cast<std::size_t> (i)] == static_cast<float> (reading.start + i));
            }
            if (loop && callback > 0 && pre.project == 0) { ++wraps; assert (! preReading.valid); }
            // A project certificate never turns a missing audition clock into permission.
            const auto audition = auditionClock.next ({ 0, hypha::AuxiliaryClockSource::vst3Continuous, false }, frames);
            assert (! audition.valid);
        }
        assert (measured > 400 && (! loop || wraps > 3));
    }
    CertifiedProjectAnalysisClock clock;
    BlockClock block;
    block.frames = frames; block.playing = block.projectValid = true;
    block.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::certifiedProject);
    assert (clock.observe (block, rate, true).valid);
    block.project += frames; assert (clock.observe (block, rate, true).valid);
    const auto old = clock.observe (block, rate, true); assert (! old.valid); // repeated position
    block.project += frames; assert (clock.observe (block, rate, true).valid);
    block.project += delay; assert (! clock.observe (block, rate, true).valid); // PDC coordinate change
    block.project += frames; const auto renewed = clock.observe (block, rate, true); assert (renewed.valid);
    block.afterGap = true; assert (! clock.observe (block, rate, true).valid);
    block.afterGap = false; block.projectValid = false; assert (! clock.observe (block, rate, true).valid);
    block.projectValid = true; block.playing = false; assert (! clock.observe (block, rate, true).valid);
    block.playing = true; assert (! clock.observe (block, rate, false).valid); // bypass/offline/format
    block.clockAuthority = 0; assert (! clock.observe (block, rate, true).valid);
    block.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::certifiedContent);
    assert (! clock.observe (block, rate, true).valid); // Studio clock keeps its own proof
    std::cout << "REAPER measurement clock: compensated 4096 PCM, loops, discontinuities and audition refusal PASS\n";
}
