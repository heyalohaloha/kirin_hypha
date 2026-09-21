#include "ReferenceContentCorrelation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>

namespace hypha::reference_audition
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        using Complex = std::complex<double>;

        void transform (std::vector<Complex>& values, bool inverse)
        {
            const auto size = values.size();
            for (size_t index = 1, reversed = 0; index < size; ++index)
            {
                auto bit = size >> 1;
                for (; (reversed & bit) != 0; bit >>= 1) reversed ^= bit;
                reversed ^= bit;
                if (index < reversed) std::swap (values[index], values[reversed]);
            }
            for (size_t width = 2; width <= size; width <<= 1)
            {
                const auto rotation = std::polar (1.0, (inverse ? 2.0 : -2.0) * pi / static_cast<double> (width));
                for (size_t first = 0; first < size; first += width)
                {
                    Complex phase { 1.0, 0.0 };
                    for (size_t index = 0; index < width / 2; ++index)
                    {
                        const auto left = values[first + index];
                        const auto right = values[first + index + width / 2] * phase;
                        values[first + index] = left + right;
                        values[first + index + width / 2] = left - right;
                        phase *= rotation;
                    }
                }
            }
            if (inverse)
                for (auto& value : values) value /= static_cast<double> (size);
        }

        // Identical zero-phase analysis filters on both observations. Preserve
        // each channel separately at the caller: stereo anti-phase must not cancel.
        std::vector<double> bandLimited (const std::vector<float>& input, int rate, bool timingDetail)
        {
            std::vector<double> values (input.begin(), input.end());
            // Emphasise timing detail above the bass region. Low-end EQ phase
            // must not pull the estimated musical position by a sample.
            const auto low = 1.0 - std::exp (-2.0 * pi * (timingDetail ? 700.0 : 180.0) / rate);
            const auto high = 1.0 - std::exp (-2.0 * pi * std::min (6000.0, rate * 0.4) / rate);
            for (int pass = 0; pass < 2; ++pass)
            {
                double lowState = 0.0;
                double highState = 0.0;
                for (size_t index = 0; index < values.size(); ++index)
                {
                    const auto position = pass == 0 ? index : values.size() - index - 1;
                    lowState += low * (values[position] - lowState);
                    highState += high * (values[position] - lowState - highState);
                    values[position] = highState;
                }
            }
            return values;
        }

        std::vector<double> prefixEnergy (const std::vector<double>& values)
        {
            std::vector<double> result (values.size() + 1, 0.0);
            for (size_t index = 0; index < values.size(); ++index)
                result[index + 1] = result[index] + values[index] * values[index];
            return result;
        }

        double centeredCorrelation (const std::vector<double>& left,
                                    const std::vector<double>& right)
        {
            if (left.size() != right.size() || left.size() < 8)
                return -1.0;
            const auto meanLeft = std::accumulate (left.begin(), left.end(), 0.0)
                / static_cast<double> (left.size());
            const auto meanRight = std::accumulate (right.begin(), right.end(), 0.0)
                / static_cast<double> (right.size());
            double dot = 0.0, leftEnergy = 0.0, rightEnergy = 0.0;
            for (size_t index = 0; index < left.size(); ++index)
            {
                const auto a = left[index] - meanLeft;
                const auto b = right[index] - meanRight;
                dot += a * b;
                leftEnergy += a * a;
                rightEnergy += b * b;
            }
            return leftEnergy > 1.0e-8 && rightEnergy > 1.0e-8
                ? dot / std::sqrt (leftEnergy * rightEnergy) : -1.0;
        }

        std::vector<double> energyEnvelope (const std::vector<float>& input,
                                            int channels, int windowFrames)
        {
            const auto frames = input.size() / static_cast<size_t> (channels);
            const auto windows = frames / static_cast<size_t> (windowFrames);
            std::vector<double> result;
            result.reserve (windows);
            for (size_t window = 0; window < windows; ++window)
            {
                double energy = 0.0;
                const auto first = window * static_cast<size_t> (windowFrames);
                for (int frame = 0; frame < windowFrames; ++frame)
                    for (int channel = 0; channel < channels; ++channel)
                    {
                        const auto value = input[((first + static_cast<size_t> (frame))
                            * static_cast<size_t> (channels)) + static_cast<size_t> (channel)];
                        energy += static_cast<double> (value) * value;
                    }
                result.push_back (10.0 * std::log10 (std::max (1.0e-14,
                    energy / static_cast<double> (windowFrames * channels))));
            }
            return result;
        }

        std::vector<double> firstDifference (const std::vector<double>& values)
        {
            std::vector<double> result;
            if (values.size() < 2)
                return result;
            result.reserve (values.size() - 1);
            for (size_t index = 1; index < values.size(); ++index)
                result.push_back (values[index] - values[index - 1]);
            return result;
        }

        std::vector<std::vector<double>> bandEnvelopes (
            const std::vector<float>& input, int sampleRate, int channels)
        {
            constexpr int bandCount = 4;
            constexpr int windowMilliseconds = 50;
            const auto windowFrames = std::max (1, sampleRate * windowMilliseconds / 1'000);
            const auto frames = input.size() / static_cast<size_t> (channels);
            const auto windows = frames / static_cast<size_t> (windowFrames);
            std::vector<std::vector<double>> result (
                static_cast<size_t> (channels * bandCount), std::vector<double> (windows));
            const std::array<double, 3> alpha {
                1.0 - std::exp (-2.0 * pi * 120.0 / sampleRate),
                1.0 - std::exp (-2.0 * pi * 1'000.0 / sampleRate),
                1.0 - std::exp (-2.0 * pi * std::min (6'000.0, sampleRate * 0.375)
                                / sampleRate),
            };
            std::array<std::array<double, 3>, 2> filters {};
            std::array<std::array<double, bandCount>, 2> energy {};
            for (size_t frame = 0; frame < windows * static_cast<size_t> (windowFrames); ++frame)
            {
                for (int channel = 0; channel < channels; ++channel)
                {
                    const auto value = static_cast<double> (input[(frame
                        * static_cast<size_t> (channels)) + static_cast<size_t> (channel)]);
                    for (size_t band = 0; band < alpha.size(); ++band)
                        filters[static_cast<size_t> (channel)][band] += alpha[band]
                            * (value - filters[static_cast<size_t> (channel)][band]);
                    const auto& filtered = filters[static_cast<size_t> (channel)];
                    const std::array<double, bandCount> values {
                        filtered[0], filtered[1] - filtered[0],
                        filtered[2] - filtered[1], value - filtered[2],
                    };
                    for (size_t band = 0; band < values.size(); ++band)
                        energy[static_cast<size_t> (channel)][band] += values[band] * values[band];
                }
                if ((frame + 1) % static_cast<size_t> (windowFrames) != 0)
                    continue;
                const auto window = (frame + 1) / static_cast<size_t> (windowFrames) - 1;
                for (int channel = 0; channel < channels; ++channel)
                    for (int band = 0; band < bandCount; ++band)
                    {
                        auto& sum = energy[static_cast<size_t> (channel)][static_cast<size_t> (band)];
                        result[static_cast<size_t> (channel * bandCount + band)][window]
                            = 10.0 * std::log10 (std::max (1.0e-14,
                                sum / static_cast<double> (windowFrames)));
                        sum = 0.0;
                    }
            }
            return result;
        }
    }

    double ContentEnvelopeCorrelation::score() const noexcept
    {
        return std::min ({ fastCorrelation, slowCorrelation,
                           onsetCorrelation, bandMedianCorrelation });
    }

    ContentCorrelation correlateReferenceContent (
        const std::vector<float>& a, const std::vector<float>& b,
        int sampleRate, int maximumLagSamples, bool timingDetail)
    {
        ContentCorrelation result;
        if (sampleRate < 8000 || sampleRate > 768000 || a.size() != b.size()
            || a.size() < static_cast<size_t> (sampleRate / 2)
            || a.size() > 1'048'576 || maximumLagSamples < 1
            || static_cast<size_t> (maximumLagSamples) >= a.size() / 3)
            return result;
        for (size_t index = 0; index < a.size(); ++index)
            if (! std::isfinite (a[index]) || ! std::isfinite (b[index])) return result;
        const auto left = bandLimited (a, sampleRate, timingDetail);
        const auto right = bandLimited (b, sampleRate, timingDetail);
        const auto leftEnergy = prefixEnergy (left);
        const auto rightEnergy = prefixEnergy (right);
        if (leftEnergy.back() < 1.0e-10 || rightEnergy.back() < 1.0e-10) return result;
        size_t fftSize = 1;
        while (fftSize < a.size() * 2) fftSize <<= 1;
        std::vector<Complex> spectrumA (fftSize), spectrumB (fftSize);
        for (size_t index = 0; index < a.size(); ++index)
        {
            spectrumA[index] = left[index];
            spectrumB[index] = right[index];
        }
        transform (spectrumA, false);
        transform (spectrumB, false);
        for (size_t index = 0; index < fftSize; ++index)
            spectrumA[index] = std::conj (spectrumA[index]) * spectrumB[index];
        transform (spectrumA, true);
        std::vector<double> scores (static_cast<size_t> (2 * maximumLagSamples + 1), 0.0);
        for (int lag = -maximumLagSamples; lag <= maximumLagSamples; ++lag)
        {
            const auto shift = static_cast<size_t> (std::abs (lag));
            const auto aStart = lag < 0 ? shift : 0;
            const auto bStart = lag > 0 ? shift : 0;
            const auto count = a.size() - shift;
            const auto energy = (leftEnergy[aStart + count] - leftEnergy[aStart])
                              * (rightEnergy[bStart + count] - rightEnergy[bStart]);
            const auto correlationIndex = lag < 0 ? fftSize - shift : shift;
            const auto score = energy > 1.0e-20
                ? std::min (1.0, std::abs (spectrumA[correlationIndex].real()) / std::sqrt (energy)) : 0.0;
            scores[static_cast<size_t> (lag + maximumLagSamples)] = score;
            if (score > result.correlation)
            {
                result.correlation = score;
                result.offsetSamples = lag;
            }
        }
        double second = 0.0;
        const auto exclusion = std::max (1, sampleRate / 500);
        for (int lag = -maximumLagSamples; lag <= maximumLagSamples; ++lag)
            if (std::abs (lag - result.offsetSamples) > exclusion)
                second = std::max (second, scores[static_cast<size_t> (lag + maximumLagSamples)]);
        result.ambiguityDb = second > 0.0 ? 20.0 * std::log10 (result.correlation / second) : 300.0;
        result.accepted = result.correlation >= 0.75 && result.ambiguityDb >= 3.0
                       && std::abs (result.offsetSamples) < maximumLagSamples;
        return result;
    }

    ContentEnvelopeCorrelation correlateReferenceEnvelope (
        const std::vector<float>& a, const std::vector<float>& b,
        int sampleRate, int channels)
    {
        ContentEnvelopeCorrelation result;
        if (sampleRate < 8'000 || sampleRate > 768'000
            || (channels != 1 && channels != 2) || a.size() != b.size()
            || a.size() < static_cast<size_t> (sampleRate * channels * 3)
            || a.size() > static_cast<size_t> (4'194'304 * channels)
            || a.size() % static_cast<size_t> (channels) != 0)
            return result;
        for (size_t index = 0; index < a.size(); ++index)
            if (! std::isfinite (a[index]) || ! std::isfinite (b[index]))
                return result;

        const auto fastA = energyEnvelope (a, channels, std::max (1, sampleRate / 50));
        const auto fastB = energyEnvelope (b, channels, std::max (1, sampleRate / 50));
        const auto mediumA = energyEnvelope (a, channels, std::max (1, sampleRate / 20));
        const auto mediumB = energyEnvelope (b, channels, std::max (1, sampleRate / 20));
        const auto slowA = energyEnvelope (a, channels, std::max (1, sampleRate / 10));
        const auto slowB = energyEnvelope (b, channels, std::max (1, sampleRate / 10));
        result.fastCorrelation = centeredCorrelation (fastA, fastB);
        result.slowCorrelation = centeredCorrelation (slowA, slowB);
        result.onsetCorrelation = centeredCorrelation (
            firstDifference (mediumA), firstDifference (mediumB));

        const auto bandsA = bandEnvelopes (a, sampleRate, channels);
        const auto bandsB = bandEnvelopes (b, sampleRate, channels);
        std::vector<double> bandScores;
        for (size_t band = 0; band < bandsA.size(); ++band)
        {
            const auto score = centeredCorrelation (bandsA[band], bandsB[band]);
            if (score >= -0.999)
                bandScores.push_back (score);
            if (score >= 0.65)
                ++result.agreeingBands;
        }
        if (! bandScores.empty())
        {
            std::sort (bandScores.begin(), bandScores.end());
            result.bandMedianCorrelation = bandScores[bandScores.size() / 2];
        }
        const auto requiredBands = channels == 2 ? 4 : 2;
        result.accepted = result.fastCorrelation >= 0.68
                       && result.slowCorrelation >= 0.72
                       && result.onsetCorrelation >= 0.55
                       && result.bandMedianCorrelation >= 0.72
                       && result.agreeingBands >= requiredBands;
        return result;
    }
}
