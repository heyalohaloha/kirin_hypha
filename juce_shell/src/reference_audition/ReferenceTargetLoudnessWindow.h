#pragma once
#include "ReferenceLiveWindowLoudness.h"
#include <array>
#include <cmath>
#include <deque>

namespace hypha::reference_audition
{
// BS.1770 の K 特性（shelf と RLB）。KirinFingerprintMeter と同じ式。状態は ch ごとに呼ぶ側が持つ。
struct KWeighting
{
    using State = std::array<double, 4>;
    std::array<double, 3> shelfB {}, shelfA {}, passA {};
    void configure (int rate)
    {
        const auto pi = std::acos (-1.0);
        const auto k = std::tan (pi * 1681.974450955533 / rate), q = 0.7071752369554196;
        const auto vh = std::pow (10.0, 3.999843853973347 / 20.0), vb = std::pow (vh, 0.4996667741545416);
        const auto a0 = 1.0 + k / q + k * k;
        shelfB = { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0 };
        shelfA = { 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        const auto r = std::tan (pi * 38.13547087602444 / rate), rq = 0.5003270373238773;
        const auto r0 = 1.0 + r / rq + r * r;
        passA = { 1.0, 2.0 * (r * r - 1.0) / r0, (1.0 - r / rq + r * r) / r0 };
    }
    double filter (double sample, State& state) const
    {
        const auto shelf = shelfB[0] * sample + state[0];
        state[0] = shelfB[1] * sample - shelfA[1] * shelf + state[1];
        state[1] = shelfB[2] * sample - shelfA[2] * shelf;
        const auto output = shelf + state[2];
        state[2] = -2.0 * shelf - passA[1] * output + state[3];
        state[3] = shelf - passA[2] * output;
        return output;
    }
};

// Worker-only K-weighted window facts. A tick is an affine state transition and a quadratic
// energy function of its initial filter state. Composing those numbers from a zero state
// excludes all audio before the selected window, without retaining or replaying its PCM.
class TargetLoudnessWindow
{
    using State = std::array<double, 4>;
    using Matrix = std::array<State, 4>;
    struct Tick
    {
        std::array<double, 2> energy {};
        std::array<State, 2> cross {}, end {};
    };
public:
    void reset() { ticks.clear(); }
    // Exactly one completed 100 ms tick; the caller shares the spectrum/TP boundaries.
    void push (const float* pcm, int nextRate, int nextChannels)
    {
        if (rate != nextRate || channels != nextChannels) configure (nextRate, nextChannels);
        Tick tick;
        for (int c = 0; c < channels; ++c)
        {
            auto& state = tick.end[static_cast<size_t> (c)];
            for (size_t n = 0; n < response.size(); ++n)
            {
                const auto value = filter (pcm[n * static_cast<size_t> (channels) + static_cast<size_t> (c)], state);
                tick.energy[static_cast<size_t> (c)] += value * value;
                for (size_t i = 0; i < 4; ++i) tick.cross[static_cast<size_t> (c)][i] += value * response[n][i];
            }
        }
        if (ticks.size() == 6000) ticks.pop_front();
        ticks.push_back (tick);
    }
    LiveWindowLoudness window (int count) const
    {
        if (count < 4 || static_cast<size_t> (count) > ticks.size()) return {};
        std::array<State, 2> states {};
        std::array<double, 4> energies {};
        std::vector<double> momentary;
        for (int index = 0; index < count; ++index)
        {
            const auto& tick = ticks[ticks.size() - static_cast<size_t> (count - index)];
            double energy = 0.0;
            for (int c = 0; c < channels; ++c)
            {
                const auto ch = static_cast<size_t> (c);
                const auto& initial = states[ch];
                auto next = tick.end[ch];
                double power = tick.energy[ch];
                for (size_t i = 0; i < 4; ++i)
                {
                    power += 2.0 * tick.cross[ch][i] * initial[i];
                    for (size_t j = 0; j < 4; ++j)
                    {
                        power += initial[i] * gram[i][j] * initial[j];
                        next[i] += transition[i][j] * initial[j];
                    }
                }
                energy += std::max (0.0, power);
                states[ch] = next;
            }
            energies[static_cast<size_t> (index % 4)] = energy;
            if (index < 3) continue;
            double sum = 0.0;
            for (const auto value : energies) sum += value;
            momentary.push_back (sum > 0.0 ? -0.691 + 10.0 * std::log10 (sum / (4.0 * response.size()))
                                          : std::numeric_limits<double>::quiet_NaN());
        }
        auto result = gatedWindowLoudness (momentary);
        result.blocks = count;
        return result;
    }
private:
    int rate = 0, channels = 0;
    KWeighting weighting;
    std::vector<State> response;
    Matrix gram {}, transition {};
    std::deque<Tick> ticks;
    double filter (double sample, State& state) const { return weighting.filter (sample, state); }
    void configure (int nextRate, int nextChannels)
    {
        reset(); rate = nextRate; channels = nextChannels;
        weighting.configure (rate);  // no change to A's meters
        response.assign (static_cast<size_t> (rate / 10), {});
        gram = {}; transition = {};
        std::array<State, 4> basis {};
        for (size_t i = 0; i < 4; ++i) basis[i][i] = 1.0;
        for (auto& sample : response)
        {
            for (size_t i = 0; i < 4; ++i) sample[i] = filter (0.0, basis[i]);
            for (size_t i = 0; i < 4; ++i)
                for (size_t j = 0; j < 4; ++j) gram[i][j] += sample[i] * sample[j];
        }
        for (size_t i = 0; i < 4; ++i)
            for (size_t j = 0; j < 4; ++j) transition[i][j] = basis[j][i];
    }
};
}
