#include "ReferenceKirinFingerprint.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hypha::reference_audition
{
namespace
{
constexpr double tau = 6.283185307179586476925286766559;
constexpr double pi = tau / 2.0;
constexpr double minimumHz = 55.0, maximumHz = 5'000.0;
constexpr int smoothHalf = 2;              // 0.5 秒 = 前後 2 区切り
constexpr double voicedLufs = -50.0;
constexpr int coarseStep = 5, maximumOffsetTicks = 300, fineTicks = 4, minimumOverlapTicks = 100;
constexpr double tieAgreement = 0.01;

int nextPowerOfTwo (int value) noexcept
{
    int result = 1;
    while (result < value) result <<= 1;
    return result;
}

// JavaScript の Math.round と同じ丸め（0.5 は大きい方へ）。
double jsRound (double value) noexcept { return std::floor (value + 0.5); }

float quantizedLufs (double lufs) noexcept
{
    // Kirin OS と同じ 2 段：LUFS-M を millilu の整数にし（ranges の lufs_m_millilu）、その値から 1 バイトの
    // round((millilu / 1000 + 70) × 2) を 0〜255 に。無しは 0（= −70）。1 段で丸めると境目の 0.0005 LU で違う。
    if (! std::isfinite (lufs)) return -70.0f;
    const auto millilu = jsRound (lufs * 1000.0);
    const auto byte = std::clamp (jsRound ((millilu / 1000.0 + 70.0) * 2.0), 0.0, 255.0);
    return static_cast<float> (byte / 2.0 - 70.0);
}

int popcount (std::uint16_t value) noexcept
{
    int count = 0;
    for (auto rest = value; rest != 0; rest = static_cast<std::uint16_t> (rest & (rest - 1))) ++count;
    return count;
}

struct Agreement { double agreement = 0.0; int count = 0; };

// a の鳴っている範囲（[first, last)）。曲の頭からの位置に置いた A の直近だけの並びでも速く回すため。
struct Voiced { int first = 0, last = 0; };

Voiced voicedRange (const KirinFingerprint& print)
{
    Voiced range;
    const auto ticks = static_cast<int> (print.lufs.size());
    while (range.first < ticks && print.lufs[static_cast<size_t> (range.first)] <= voicedLufs) ++range.first;
    range.last = ticks;
    while (range.last > range.first && print.lufs[static_cast<size_t> (range.last - 1)] <= voicedLufs) --range.last;
    return range;
}

Agreement agreementAt (const KirinFingerprint& a, const KirinFingerprint& b, int offset, int step, Voiced range)
{
    const auto bTicks = static_cast<int> (b.bits.size());
    int differing = 0, count = 0;
    // Kirin OS と同じ並び（0 から step ごと）を保つため、始まりは step の倍数に揃える。
    auto from = std::max (0, -offset);
    if (range.first > from) from += (range.first - from + step - 1) / step * step;
    for (int tick = from; tick < range.last && tick + offset < bTicks; tick += step)
    {
        const auto at = static_cast<size_t> (tick), bt = static_cast<size_t> (tick + offset);
        if (a.lufs[at] <= voicedLufs || b.lufs[bt] <= voicedLufs) continue;
        differing += popcount (static_cast<std::uint16_t> (a.bits[at] ^ b.bits[bt]));
        ++count;
    }
    return { count > 0 ? 1.0 - static_cast<double> (differing) / (12.0 * count) : 0.0, count };
}

double loudnessCorrelation (const KirinFingerprint& a, const KirinFingerprint& b, int offset, Voiced range)
{
    const auto bTicks = static_cast<int> (b.bits.size());
    double sumX = 0.0, sumY = 0.0;
    int count = 0;
    for (int tick = std::max (range.first, -offset); tick < range.last && tick + offset < bTicks; ++tick)
    {
        const auto x = a.lufs[static_cast<size_t> (tick)], y = b.lufs[static_cast<size_t> (tick + offset)];
        if (x <= voicedLufs || y <= voicedLufs) continue;
        sumX += x; sumY += y; ++count;
    }
    if (count == 0) return 0.0;
    const auto meanX = sumX / count, meanY = sumY / count;
    double sxy = 0.0, sxx = 0.0, syy = 0.0;
    for (int tick = std::max (range.first, -offset); tick < range.last && tick + offset < bTicks; ++tick)
    {
        const auto x = a.lufs[static_cast<size_t> (tick)], y = b.lufs[static_cast<size_t> (tick + offset)];
        if (x <= voicedLufs || y <= voicedLufs) continue;
        sxy += (x - meanX) * (y - meanY); sxx += (x - meanX) * (x - meanX); syy += (y - meanY) * (y - meanY);
    }
    return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt (sxx * syy) : 0.0;
}

int voicedTicks (const KirinFingerprint& print)
{
    return static_cast<int> (std::count_if (print.lufs.begin(), print.lufs.end(), [] (float value) { return value > voicedLufs; }));
}
}

FingerprintMatch compareFingerprints (const KirinFingerprint& a, const KirinFingerprint& b)
{
    return compareFingerprints (a, b, -maximumOffsetTicks, maximumOffsetTicks);
}

FingerprintMatch compareFingerprints (const KirinFingerprint& a, const KirinFingerprint& b, int minimumOffset, int maximumOffset)
{
    FingerprintMatch result;
    if (a.bits.size() != a.lufs.size() || b.bits.size() != b.lufs.size()) return result;
    const auto required = std::max (static_cast<double> (minimumOverlapTicks), std::min (voicedTicks (a), voicedTicks (b)) / 2.0);
    const auto range = voicedRange (a);
    struct Coarse { double agreement; int offset; };
    std::vector<Coarse> coarse;
    for (int offset = minimumOffset; offset <= maximumOffset; ++offset)
    {
        const auto found = agreementAt (a, b, offset, coarseStep, range);
        if (found.count * coarseStep >= required) coarse.push_back ({ found.agreement, offset });
    }
    if (coarse.empty()) return result;  // 一緒に鳴っている区切りが足りない
    double top = 0.0;
    for (const auto& item : coarse) top = std::max (top, item.agreement);
    // 繰り返しの曲は 1 回ぶんずれても同じくらい合うので、最良から 0.01 以内では音量の流れで選ぶ。
    int chosen = 0;
    double bestLoudness = -std::numeric_limits<double>::infinity();
    for (const auto& item : coarse)
        if (item.agreement >= top - tieAgreement)
            if (const auto loudness = loudnessCorrelation (a, b, item.offset, range); loudness > bestLoudness)
            {
                bestLoudness = loudness;
                chosen = item.offset;
            }
    Agreement fine {};
    int fineOffset = chosen;
    bool fineFound = false;
    for (int offset = chosen - fineTicks; offset <= chosen + fineTicks; ++offset)
    {
        const auto found = agreementAt (a, b, offset, 1, range);
        if (found.count >= required && (! fineFound || found.agreement > fine.agreement))
        {
            fine = found;
            fineOffset = offset;
            fineFound = true;
        }
    }
    if (! fineFound) fine = agreementAt (a, b, chosen, 1, range);
    result.agreement = fine.agreement;
    result.loudnessCorrelation = loudnessCorrelation (a, b, fineOffset, range);
    result.offsetTicks = fineOffset;
    result.relation = result.agreement >= 0.84 && result.loudnessCorrelation >= 0.95 ? FingerprintMatch::Relation::nearIdentical
        : result.agreement >= 0.70 || (result.agreement >= 0.62 && result.loudnessCorrelation >= 0.5) ? FingerprintMatch::Relation::sameSong
        : FingerprintMatch::Relation::different;
    return result;
}

KirinFingerprint decodeFingerprint (const juce::MemoryBlock& chromaSigns, const juce::MemoryBlock& loudness, std::int64_t ticks)
{
    KirinFingerprint result;
    if (ticks <= 0 || chromaSigns.getSize() != static_cast<size_t> (ticks) * 2 || loudness.getSize() != static_cast<size_t> (ticks))
        return result;
    const auto* signs = static_cast<const std::uint8_t*> (chromaSigns.getData());
    const auto* levels = static_cast<const std::uint8_t*> (loudness.getData());
    for (std::int64_t tick = 0; tick < ticks; ++tick)
    {
        const auto at = static_cast<size_t> (tick);
        result.bits.push_back (static_cast<std::uint16_t> ((signs[at * 2] | (signs[at * 2 + 1] << 8)) & 0x0fff));
        result.lufs.push_back (static_cast<float> (levels[at] / 2.0 - 70.0));
    }
    return result;
}

KirinFingerprint fingerprintFrom (const std::vector<std::array<double, 12>>& chroma, const std::vector<double>& lufs)
{
    KirinFingerprint result;
    const auto ticks = static_cast<int> (std::min (chroma.size(), lufs.size()));
    for (int tick = 0; tick < ticks; ++tick)
    {
        std::array<double, 12> sum {};
        for (int k = std::max (0, tick - smoothHalf); k <= std::min (ticks - 1, tick + smoothHalf); ++k)
            for (size_t pitch = 0; pitch < 12; ++pitch) sum[pitch] += chroma[static_cast<size_t> (k)][pitch];
        std::array<double, 12> logs {};
        double mean = 0.0;
        for (size_t pitch = 0; pitch < 12; ++pitch)
        {
            logs[pitch] = std::log10 (sum[pitch] + 1.0e-30);
            mean += logs[pitch] / 12.0;
        }
        std::uint16_t bits = 0;
        for (size_t pitch = 0; pitch < 12; ++pitch)
            if (logs[pitch] > mean) bits = static_cast<std::uint16_t> (bits | (1u << pitch));
        result.bits.push_back (bits);
        result.lufs.push_back (quantizedLufs (lufs[static_cast<size_t> (tick)]));
    }
    return result;
}

bool KirinFingerprintMeter::configure (int sampleRate, int channels, int maximumTicks)
{
    if (sampleRate < 40'000 || sampleRate > 768'000 || channels < 1 || channels > 2 || maximumTicks < 1 || maximumTicks > 12'000)
        return false;
    rate = sampleRate;
    channelCount = channels;
    tickLength = sampleRate / 10;
    size = std::max (2048, nextPowerOfTwo (sampleRate / 6));
    capacity = maximumTicks;
    ring.assign (static_cast<size_t> (size), 0.0);
    hann.resize (static_cast<size_t> (size));
    for (int i = 0; i < size; ++i)
        hann[static_cast<size_t> (i)] = 0.5 - 0.5 * std::cos (tau * static_cast<double> (i) / static_cast<double> (size));
    twiddle.resize (static_cast<size_t> (size / 2));
    for (int k = 0; k < size / 2; ++k)
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
    weights.clear();
    for (int bin = 1; bin <= size / 2; ++bin)
    {
        const auto hz = static_cast<double> (bin) * static_cast<double> (sampleRate) / static_cast<double> (size);
        if (hz < minimumHz || hz > maximumHz) continue;
        const auto pitch = 12.0 * std::log2 (hz / 440.0) + 69.0;
        const auto lower = std::floor (pitch);
        const auto fraction = pitch - lower;
        const auto pitchClass = static_cast<int> (((static_cast<long long> (lower) % 12) + 12) % 12);
        weights.push_back ({ bin, pitchClass, 1.0 - fraction, fraction });
    }
    // BS.1770 の K 特性（前段の shelf と RLB の high-pass）。係数はサンプルレートごとに双一次変換で作る。
    {
        const auto f0 = 1681.974450955533, gain = 3.999843853973347, q = 0.7071752369554196;
        const auto k = std::tan (pi * f0 / sampleRate);
        const auto vh = std::pow (10.0, gain / 20.0), vb = std::pow (vh, 0.4996667741545416);
        const auto a0 = 1.0 + k / q + k * k;
        shelfB = { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0, 0.0, 0.0 };
        shelfA = { 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0, 0.0, 0.0 };
    }
    {
        const auto f0 = 38.13547087602444, q = 0.5003270373238773;
        const auto k = std::tan (pi * f0 / sampleRate);
        passB = { 1.0, -2.0, 1.0, 0.0, 0.0 };
        passA = { 1.0, 2.0 * (k * k - 1.0) / (1.0 + k / q + k * k), (1.0 - k / q + k * k) / (1.0 + k / q + k * k), 0.0, 0.0 };
    }
    shelfState.assign (static_cast<size_t> (channels), {});
    passState.assign (static_cast<size_t> (channels), {});
    squares.assign (static_cast<size_t> (sampleRate * 4 / 10), 0.0);
    chroma.assign (static_cast<size_t> (capacity), {});
    loudness.assign (static_cast<size_t> (capacity), std::numeric_limits<double>::quiet_NaN());
    reset();
    return true;
}

void KirinFingerprintMeter::reset() noexcept
{
    std::fill (ring.begin(), ring.end(), 0.0);
    ringWrite = sinceTick = 0;
    for (auto& state : shelfState) state = {};
    for (auto& state : passState) state = {};
    std::fill (squares.begin(), squares.end(), 0.0);
    squaresWrite = squaresHeld = 0;
    head = held = 0;
}

double KirinFingerprintMeter::filter (int channel, double sample) noexcept
{
    // 直接形 II 転置の biquad を 2 段（shelf → high-pass）。
    auto& s = shelfState[static_cast<size_t> (channel)];
    const auto shelved = shelfB[0] * sample + s[0];
    s[0] = shelfB[1] * sample - shelfA[1] * shelved + s[1];
    s[1] = shelfB[2] * sample - shelfA[2] * shelved;
    auto& p = passState[static_cast<size_t> (channel)];
    const auto passed = passB[0] * shelved + p[0];
    p[0] = passB[1] * shelved - passA[1] * passed + p[1];
    p[1] = passB[2] * shelved - passA[2] * passed;
    return passed;
}

void KirinFingerprintMeter::push (const float* interleaved, int frames) noexcept
{
    if (rate == 0 || interleaved == nullptr) return;
    for (int frame = 0; frame < frames; ++frame)
    {
        double mono = 0.0, energy = 0.0;
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto sample = static_cast<double> (interleaved[frame * channelCount + channel]);
            mono += sample;
            const auto weighted = filter (channel, sample);
            energy += weighted * weighted;
        }
        ring[static_cast<size_t> (ringWrite)] = mono / channelCount;
        ringWrite = (ringWrite + 1) % size;
        squares[static_cast<size_t> (squaresWrite)] = energy;
        squaresWrite = (squaresWrite + 1) % static_cast<int> (squares.size());
        squaresHeld = std::min (squaresHeld + 1, static_cast<int> (squares.size()));
        if (++sinceTick == tickLength)
        {
            tick();
            sinceTick = 0;
        }
    }
}

void KirinFingerprintMeter::fft() noexcept
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

void KirinFingerprintMeter::tick() noexcept
{
    // クロマ：直近 N サンプル（左右の平均）に Hann を掛けた FFT の power を、近い 2 音名へ分ける。
    for (int i = 0; i < size; ++i)
        spectrum[static_cast<size_t> (i)] = { ring[static_cast<size_t> ((ringWrite + i) % size)] * hann[static_cast<size_t> (i)], 0.0 };
    fft();
    auto& energies = chroma[static_cast<size_t> (head)];
    energies = {};
    for (const auto& weight : weights)
    {
        const auto power = std::norm (spectrum[static_cast<size_t> (weight.bin)]);
        energies[static_cast<size_t> (weight.pitch)] += power * weight.lower;
        energies[static_cast<size_t> ((weight.pitch + 1) % 12)] += power * weight.upper;
    }
    // LUFS-M：直近 400 ms の K 特性の二乗の平均（ch は足す）。400 ms に満たなければ無し。
    double sum = 0.0;
    for (const auto value : squares) sum += value;
    loudness[static_cast<size_t> (head)] = squaresHeld < static_cast<int> (squares.size())
        ? std::numeric_limits<double>::quiet_NaN()
        : -0.691 + 10.0 * std::log10 (std::max (sum / static_cast<double> (squares.size()), 1.0e-30));
    head = (head + 1) % capacity;
    held = std::min (held + 1, capacity);
}

KirinFingerprint KirinFingerprintMeter::fingerprint (int ticks) const
{
    const int count = std::min (held, ticks);
    std::vector<std::array<double, 12>> energies;
    std::vector<double> levels;
    for (int index = count - 1; index >= 0; --index)
    {
        const auto slot = static_cast<size_t> ((head - 1 - index + capacity) % capacity);
        energies.push_back (chroma[slot]);
        levels.push_back (loudness[slot]);
    }
    return fingerprintFrom (energies, levels);
}

bool KirinFingerprintMeter::lastTick (std::array<double, 12>& chromaDb, double& lufs) const noexcept
{
    if (held == 0) return false;
    const auto slot = static_cast<size_t> ((head - 1 + capacity) % capacity);
    for (size_t pitch = 0; pitch < 12; ++pitch) chromaDb[pitch] = 10.0 * std::log10 (std::max (chroma[slot][pitch], 1.0e-30));
    lufs = loudness[slot];
    return true;
}
}
