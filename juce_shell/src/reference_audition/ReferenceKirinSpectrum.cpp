#include "ReferenceKirinSpectrum.h"

#include <algorithm>
#include <cmath>

namespace hypha::reference_audition
{
namespace
{
constexpr double tau = 6.283185307179586476925286766559;
// Kirin OS は振幅を 1e-15 で下を切る（−300 dBFS）。power ではその二乗。
constexpr double floorPower = 1.0e-30;

int nextPowerOfTwo (int value) noexcept
{
    int result = 1;
    while (result < value) result <<= 1;
    return result;
}

double powerDb (double meanSquare) noexcept
{
    return 10.0 * std::log10 (std::max (meanSquare, floorPower));
}

// nearest-rank（Kirin OS の Cue の集計と同じ：並べた値の round((n − 1)·ratio) 番目）。窓は最大 10 分
// （6,000 フレーム）あるので、全部は並べずにその位置だけを取り出す。
float rank (std::vector<float>& values, double ratio) noexcept
{
    const auto index = std::min (static_cast<size_t> (std::llround (static_cast<double> (values.size() - 1) * ratio)),
                                 values.size() - 1);
    std::nth_element (values.begin(), values.begin() + static_cast<std::ptrdiff_t> (index), values.end());
    return values[index];
}
}

bool KirinSpectrumMeter::configure (int sampleRate, int channels, int maximumFrames)
{
    if (sampleRate < 40'000 || sampleRate > 768'000 || channels < 1 || channels > 2
        || maximumFrames < 1 || maximumFrames > 6'000)
        return false;
    rate = sampleRate;
    channelCount = channels;
    frameLength = sampleRate / 10;
    size = std::max (2048, nextPowerOfTwo (sampleRate / 6));
    capacity = maximumFrames;
    const auto bins = size / 2;
    // 帯域の端の bin は Kirin OS と同じ順の計算（周波数 × N ÷ sr）で決める（境目の丸めを揃える）。
    const auto toBin = [this, sampleRate] (double hz) { return hz * static_cast<double> (size) / static_cast<double> (sampleRate); };

    const auto top = std::min (static_cast<double> (sampleRate) * 0.5, 20'000.0);
    centers.resize (bandCount);
    for (int band = 0; band < bandCount; ++band)
        centers[static_cast<size_t> (band)] = 20.0 * std::pow (top / 20.0, static_cast<double> (band) / 63.0);
    firstBin.assign (bandCount, 0);
    lastBin.assign (bandCount, 0);
    for (int band = 0; band < bandCount; ++band)
    {
        const auto index = static_cast<size_t> (band);
        const auto low = band == 0 ? 0.0 : std::sqrt (centers[index] * centers[index - 1]);
        const auto high = band == bandCount - 1 ? static_cast<double> (sampleRate) * 0.5
                                                : std::sqrt (centers[index] * centers[index + 1]);
        const auto nearest = static_cast<int> (std::llround (toBin (centers[index])));
        firstBin[index] = std::min (static_cast<int> (std::ceil (toBin (low))), nearest);
        lastBin[index] = std::min (std::max (static_cast<int> (std::floor (toBin (high))), nearest), bins);
    }
    balanceBand.assign (static_cast<size_t> (bins + 1), -1);
    for (int bin = 0; bin <= bins; ++bin)
    {
        const auto hz = static_cast<double> (bin) * static_cast<double> (sampleRate) / static_cast<double> (size);
        for (int band = 0; band < 4; ++band)
            if (hz >= balanceEdgesHz[static_cast<size_t> (band)] && hz < balanceEdgesHz[static_cast<size_t> (band) + 1])
                balanceBand[static_cast<size_t> (bin)] = band;
    }

    // periodic Hann（長さはフレームの長さ。FFT の残りはゼロ）。
    hann.resize (static_cast<size_t> (frameLength));
    hannSum = hannSquared = 0.0;
    for (int i = 0; i < frameLength; ++i)
    {
        const auto value = 0.5 - 0.5 * std::cos (tau * static_cast<double> (i) / static_cast<double> (frameLength));
        hann[static_cast<size_t> (i)] = value;
        hannSum += value;
        hannSquared += value * value;
    }
    twiddle.resize (static_cast<size_t> (bins));
    for (int k = 0; k < bins; ++k)
        twiddle[static_cast<size_t> (k)] = std::polar (1.0, -tau * static_cast<double> (k) / static_cast<double> (size));
    reversed.resize (static_cast<size_t> (size));
    int bitCount = 0;
    while ((1 << bitCount) < size) ++bitCount;
    for (int i = 0; i < size; ++i)
    {
        int value = 0;
        for (int bit = 0; bit < bitCount; ++bit)
            if ((i & (1 << bit)) != 0) value |= 1 << (bitCount - 1 - bit);
        reversed[static_cast<size_t> (i)] = value;
    }
    spectrum.assign (static_cast<size_t> (size), {});
    power.assign (static_cast<size_t> (bins + 1), 0.0);
    pending.assign (static_cast<size_t> (channels), std::vector<double> (static_cast<size_t> (frameLength), 0.0));
    levels.assign (static_cast<size_t> (capacity) * bandCount, 0.0f);
    balancePower.assign (static_cast<size_t> (capacity) * 4, 0.0);
    reset();
    return true;
}

void KirinSpectrumMeter::reset() noexcept
{
    pendingCount = 0;
    head = held = 0;
}

void KirinSpectrumMeter::push (const float* interleaved, int frames) noexcept
{
    if (rate == 0 || interleaved == nullptr) return;
    for (int frame = 0; frame < frames; ++frame)
    {
        for (int channel = 0; channel < channelCount; ++channel)
            pending[static_cast<size_t> (channel)][static_cast<size_t> (pendingCount)]
                = static_cast<double> (interleaved[frame * channelCount + channel]);
        if (++pendingCount == frameLength)
        {
            transform();
            pendingCount = 0;
        }
    }
}

void KirinSpectrumMeter::fft() noexcept
{
    for (int i = 0; i < size; ++i)
        if (i < reversed[static_cast<size_t> (i)])
            std::swap (spectrum[static_cast<size_t> (i)], spectrum[static_cast<size_t> (reversed[static_cast<size_t> (i)])]);
    for (int length = 2; length <= size; length <<= 1)
    {
        const int half = length / 2, stride = size / length;
        for (int start = 0; start < size; start += length)
            for (int k = 0; k < half; ++k)
            {
                auto& low = spectrum[static_cast<size_t> (start + k)];
                auto& high = spectrum[static_cast<size_t> (start + k + half)];
                const auto turned = twiddle[static_cast<size_t> (k * stride)] * high;
                high = low - turned;
                low += turned;
            }
    }
}

void KirinSpectrumMeter::transform() noexcept
{
    const auto bins = size / 2;
    std::fill (power.begin(), power.end(), 0.0);
    for (const auto& channel : pending)
    {
        for (int i = 0; i < size; ++i)
            spectrum[static_cast<size_t> (i)] = i < frameLength
                ? std::complex<double> (channel[static_cast<size_t> (i)] * hann[static_cast<size_t> (i)], 0.0)
                : std::complex<double> {};
        fft();
        for (int bin = 0; bin <= bins; ++bin)
            power[static_cast<size_t> (bin)] += std::norm (spectrum[static_cast<size_t> (bin)]) / channelCount;
    }
    const auto edgeScale = [bins] (int bin, double inner) { return bin == 0 || bin == bins ? 1.0 : inner; };
    auto* bands = levels.data() + static_cast<size_t> (head) * bandCount;
    for (int band = 0; band < bandCount; ++band)
    {
        double maximum = 0.0;
        for (int bin = firstBin[static_cast<size_t> (band)]; bin <= lastBin[static_cast<size_t> (band)]; ++bin)
            maximum = std::max (maximum, power[static_cast<size_t> (bin)] * edgeScale (bin, 4.0));
        bands[band] = static_cast<float> (powerDb (maximum / (hannSum * hannSum)));
    }
    auto* balance = balancePower.data() + static_cast<size_t> (head) * 4;
    std::fill (balance, balance + 4, 0.0);
    for (int bin = 0; bin <= bins; ++bin)
        if (const auto band = balanceBand[static_cast<size_t> (bin)]; band >= 0)
            balance[band] += power[static_cast<size_t> (bin)] * edgeScale (bin, 2.0)
                           / (static_cast<double> (size) * hannSquared);
    head = (head + 1) % capacity;
    held = std::min (held + 1, capacity);
}

std::shared_ptr<const KirinSpectrumWindow> KirinSpectrumMeter::window (int wantedFrames) const
{
    const int count = std::min (held, wantedFrames);
    if (count <= 0) return nullptr;
    auto result = std::make_shared<KirinSpectrumWindow>();
    result->centersHz = centers;
    result->frames = count;
    result->wantedFrames = wantedFrames;
    result->p10Db.resize (bandCount);
    result->medianDb.resize (bandCount);
    result->p90Db.resize (bandCount);
    std::vector<float> values (static_cast<size_t> (count));
    std::array<double, 4> sum {};
    for (int band = 0; band < bandCount; ++band)
    {
        for (int frame = 0; frame < count; ++frame)
        {
            const auto slot = (head - 1 - frame + capacity) % capacity;
            values[static_cast<size_t> (frame)] = levels[static_cast<size_t> (slot) * bandCount + static_cast<size_t> (band)];
            if (band < 4) sum[static_cast<size_t> (band)] += balancePower[static_cast<size_t> (slot) * 4 + static_cast<size_t> (band)];
        }
        result->p10Db[static_cast<size_t> (band)] = rank (values, 0.1);
        result->medianDb[static_cast<size_t> (band)] = rank (values, 0.5);
        result->p90Db[static_cast<size_t> (band)] = rank (values, 0.9);
    }
    for (size_t band = 0; band < 4; ++band)
        result->balanceDb[band] = powerDb (sum[band] / count);
    return result;
}

bool KirinSpectrumMeter::lastFrame (std::array<float, bandCount>& bands, std::array<double, 4>& balance) const noexcept
{
    if (held == 0) return false;
    const auto slot = static_cast<size_t> ((head - 1 + capacity) % capacity);
    std::copy_n (levels.data() + slot * bandCount, bandCount, bands.begin());
    for (size_t band = 0; band < 4; ++band) balance[band] = powerDb (balancePower[slot * 4 + band]);
    return true;
}
}
