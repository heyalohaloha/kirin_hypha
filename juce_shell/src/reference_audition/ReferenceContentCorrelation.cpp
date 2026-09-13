#include "ReferenceContentCorrelation.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

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
}
