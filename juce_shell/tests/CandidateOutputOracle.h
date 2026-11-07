#pragma once
#include <array>
#include <cmath>

namespace hypha::tests
{
template<class Host, class Require> void finishCandidateWithoutClock (Host& host, Require require)
{
    host.post().selectLiveComparePre (true);
    host.render (30, true, true);
    require (host.post().liveCompareStatus().preAudible, "A is actually audible before the unverified END");
    std::array<float, 2> origin {};
    for (int ch = 0; ch < host.channels; ++ch)
        origin[static_cast<std::size_t> (ch)] = host.audio.getSample (ch, host.block - 1);
    for (auto& clock : host.clocks) clock.available = false;
    const auto first = host.cursor;
    host.post().finishLiveCompare();
    host.render (1, true, false, false);
    for (int ch = 0; ch < host.channels; ++ch)
        for (int frame = 0; frame < host.block; ++frame)
        {
            const auto at = first + frame - host.delayB;
            const auto raw = std::tanh (host.history (ch, at) * 5.0f) * 0.4f
                + std::tanh (host.history (ch, at - 997) * 4.0f) * 0.08f;
            const auto weight = std::min (1.0f, static_cast<float> (frame) / 240.0f);
            const auto expected = origin[static_cast<std::size_t> (ch)] * (1.0f - weight) + raw * weight;
            require (std::abs (host.audio.getSample (ch, frame) - expected) < 4.0e-6f,
                     "END with a missing clock fades every sample to the actual chain without replaying PCM");
        }
    for (auto& clock : host.clocks) clock.available = true;
}

// Check PRE2's first unverified block independently, before B can hide a hard cut.
// Product blocks are at least 256 frames, so the complete 240-frame fade fits here.
struct CandidateReceiverOracle
{
    bool previouslyExact = false;
    std::array<float, 2> last {};
    template<class Require, class Passthrough>
    void check (Require require, const juce::AudioBuffer<float>& output, bool exact, Passthrough passthrough)
    {
        if (previouslyExact && !exact)
            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                for (int frame = 0; frame < output.getNumSamples(); ++frame)
                {
                    const auto weight = std::min (1.0f, static_cast<float> (frame) / 240.0f);
                    const auto expected = last[static_cast<std::size_t> (ch)] * (1.0f - weight)
                        + passthrough (frame, ch) * weight;
                    require (std::abs (output.getSample (ch, frame) - expected) < 4.0e-6f,
                             "PRE2's first unverified block follows the independent fade to its real input");
                }
        for (int ch = 0; ch < output.getNumChannels(); ++ch)
            last[static_cast<std::size_t> (ch)] = output.getSample (ch, output.getNumSamples() - 1);
        previouslyExact = exact;
    }
};

// Fixed-unity product scenarios check every sample, including the first block, both
// fades and fallback. B uses the actual upstream effect history; A is generated
// independently from the fixture's source and effect formula.
struct CandidateOutputOracle
{
    float sourceWeight = 0.0f;
    int transitionFrame = 240;
    std::array<float, 2> previous {}, origin {};
    template<class Require, class A, class B>
    void check (Require require, const juce::AudioBuffer<float>& output, bool accepted,
                bool selectedA, A a, B b)
    {
        if (sourceWeight > 0.0f && ! accepted) { origin = previous; transitionFrame = 0; }
        if (! accepted) sourceWeight = 0.0f;
        for (int frame = 0; frame < output.getNumSamples(); ++frame)
        {
            if (accepted)
                sourceWeight = selectedA ? std::min (1.0f, sourceWeight + 1.0f / 240.0f)
                                         : std::max (0.0f, sourceWeight - 1.0f / 240.0f);
            const auto progress = std::min (1.0f, static_cast<float> (transitionFrame) / 240.0f);
            for (int ch = 0; ch < output.getNumChannels(); ++ch)
            {
                const auto raw = sourceWeight >= 1.0f ? a (frame, ch)
                    : sourceWeight <= 0.0f ? b (frame, ch)
                    : b (frame, ch) * (1.0f - sourceWeight) + a (frame, ch) * sourceWeight;
                const auto channel = static_cast<std::size_t> (ch);
                const auto expected = transitionFrame >= 240 ? raw : origin[channel] * (1.0f - progress) + raw * progress;
                if (std::abs (output.getSample (ch, frame) - expected) >= 4.0e-6f)
                    std::cerr << "whole-output oracle: frame=" << frame << " accepted=" << accepted
                        << " weight=" << sourceWeight << " actual=" << output.getSample (ch, frame)
                        << " expected=" << expected << '\n';
                require (std::abs (output.getSample (ch, frame) - expected) < 4.0e-6f,
                         "every product output sample follows the independent path and transition oracle");
                previous[channel] = expected;
            }
            ++transitionFrame;
        }
    }
};
}

namespace hypha::tests
{
// Defined solely by the synthetic host's continuous input epoch and requested source.
// Once an exact source arrives it must stay exact until the HOST injects a discontinuity.
// No product verdict, receipt, renderer weight or actual B history chooses the expectation.
struct CandidateContinuityOracle
{
    bool enabled = false, arrived = false;
    int blocks = 0;
    void begin() noexcept { enabled = true; arrived = false; blocks = 0; }
    template<class Require, class Expected>
    void check (Require require, const juce::AudioBuffer<float>& output, Expected expected, const char* message)
    {
        if (!enabled) return;
        bool exact = true;
        for (int ch = 0; ch < output.getNumChannels(); ++ch)
            for (int frame = 0; frame < output.getNumSamples(); ++frame)
                exact = exact && std::abs (output.getSample (ch, frame) - expected (frame, ch)) < 3.0e-6f;
        require (!arrived || exact, message);
        arrived = arrived || exact;
        require (++blocks <= 32 || arrived, "continuous fixture reaches its requested source within the admission bound");
    }
};
}
