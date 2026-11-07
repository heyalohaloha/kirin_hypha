#pragma once
#include "../src/live_compare/CandidateTransition.h"

template<class Require> void verifyCandidateTransition (Require require)
{
    for (const int frames : { 1, 64, 256, 1024 })
    {
        hypha::live_compare::CandidateTransition transition;
        std::array<float, 1024> samples;
        float* io[] { samples.data() };
        samples.fill (0.4f);
        require (! transition.apply (io, 1, frames, 240, true, false), "a stable source is unchanged");
        int position = 0;
        for (int block = 0; block < 242 / frames + 2; ++block)
        {
            samples.fill (-0.2f);
            transition.apply (io, 1, frames, 240, false, block == 0);
            for (int i = 0; i < frames; ++i, ++position)
            {
                const auto weight = std::min (1.0f, static_cast<float> (position) / 240.0f);
                const auto expected = 0.4f * (1.0f - weight) - 0.2f * weight;
                require (std::abs (samples[i] - expected) < 1.0e-6f,
                         "every fallback sample follows the independent five-millisecond ramp");
            }
        }
    }
}
