#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

// Test only. Music-like stereo PRE at 48 kHz and the same music through a seven-band minimum-phase
// EQ and a brickwall limiter, as a mastering chain between PRE and POST. Deterministic, precomputed
// for one fixture clock range. The limiter's lookahead is reported and compensated: POST's audio is
// at PRE's position, so the right content offset is zero.
namespace hypha::tests::chain_music
{
constexpr double rate = 48000.0, pi = 3.14159265358979323846;

enum class Texture { band, sparse };

struct Chain
{
    double eqDb = 0.0;      // band strength: boosts and cuts of up to this many dB
    double driveDb = 0.0;   // limiter input gain; 0 leaves the limiter out
    int lookahead = 0;      // samples; 0 is an instant (clipping) gain
    double trimDb = 0.0;    // output gain after the limiter
};

inline double hash (std::uint64_t i)
{
    auto z = i + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<double> (z >> 11) / 9007199254740992.0 * 2.0 - 1.0;
}

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    // RBJ: 0 high-pass, 1 peak, 2 low shelf, 3 high shelf.
    static Biquad make (int type, double hz, double q, double dB)
    {
        const double A = std::pow (10.0, dB / 40.0), w = 2 * pi * hz / rate, c = std::cos (w), s = std::sin (w);
        const double alpha = s / (2 * q), r = 2 * std::sqrt (A) * alpha;
        double b[3], a[3];
        if (type == 0) { b[0] = (1 + c) / 2; b[1] = -(1 + c); b[2] = b[0]; a[0] = 1 + alpha; a[1] = -2 * c; a[2] = 1 - alpha; }
        else if (type == 1) { b[0] = 1 + alpha * A; b[1] = -2 * c; b[2] = 1 - alpha * A; a[0] = 1 + alpha / A; a[1] = -2 * c; a[2] = 1 - alpha / A; }
        else if (type == 2)
        {
            b[0] = A * ((A + 1) - (A - 1) * c + r); b[1] = 2 * A * ((A - 1) - (A + 1) * c); b[2] = A * ((A + 1) - (A - 1) * c - r);
            a[0] = (A + 1) + (A - 1) * c + r; a[1] = -2 * ((A - 1) + (A + 1) * c); a[2] = (A + 1) + (A - 1) * c - r;
        }
        else
        {
            b[0] = A * ((A + 1) + (A - 1) * c + r); b[1] = -2 * A * ((A - 1) + (A + 1) * c); b[2] = A * ((A + 1) + (A - 1) * c - r);
            a[0] = (A + 1) - (A - 1) * c + r; a[1] = 2 * ((A - 1) - (A + 1) * c); a[2] = (A + 1) - (A - 1) * c - r;
        }
        Biquad f; f.b0 = b[0] / a[0]; f.b1 = b[1] / a[0]; f.b2 = b[2] / a[0]; f.a1 = a[1] / a[0]; f.a2 = a[2] / a[0];
        return f;
    }
};

struct Tracks
{
    std::int64_t from = 0;
    std::vector<float> pre, post; // interleaved stereo
    double averageReductionDb = 0.0;
    float at (const std::vector<float>& x, std::int64_t clock, int channel) const
    {
        const auto i = clock - from;
        return i >= 0 && i * 2 + 1 < static_cast<std::int64_t> (x.size()) ? x[static_cast<std::size_t> (i * 2 + channel)] : 0.0f;
    }
    float preAt (std::int64_t clock, int channel) const { return at (pre, clock, channel); }
    float postAt (std::int64_t clock, int channel) const { return at (post, clock, channel); }
};

// 120 BPM. band: kick, snare, hats, saw bass, a pad chord, a lead and a room bed; most of the
// energy is in the bass. sparse: the pad and the lead alone.
inline std::vector<float> music (std::int64_t from, std::int64_t frames, Texture texture)
{
    std::vector<float> out (static_cast<std::size_t> (frames) * 2);
    const double roots[] = { 55.0, 43.65, 49.0, 41.2 };
    double bass = 0, lead = 0, snare = 0, hatIn = 0, hat = 0, bed = 0;
    for (std::int64_t n = 0; n < frames; ++n)
    {
        const auto clock = from + n;
        const double t = static_cast<double> (clock) / rate, inBeat = std::fmod (t, 0.5), inEighth = std::fmod (t, 0.25);
        const auto beat = static_cast<std::int64_t> (t / 0.5);
        const double root = roots[(beat / 4) % 4];
        double pad = 0;
        for (const double ratio : { 4.0, 5.04, 5.99 })
            for (int h = 1; h <= 6; ++h)
                pad += std::sin (2 * pi * root * ratio * h * t + h + ratio) / (h * h);
        pad *= 0.06 * (0.7 + 0.3 * std::sin (2 * pi * 0.25 * t));
        const double lf = root * 8.0 * (1.0 + 0.006 * std::sin (2 * pi * 5.5 * t)) * ((beat + beat / 4) % 3 == 0 ? 1.2 : 1.0);
        double saw = 0;
        for (int h = 1; h <= 8; ++h) saw += std::sin (2 * pi * lf * h * t) / h;
        lead += 0.2 * (saw - lead);
        const double melody = 0.08 * lead * (inEighth < 0.2 ? 1.0 : 0.3);
        double left = 1.1 * pad + 0.8 * melody, right = 0.9 * pad + 1.2 * melody;
        if (texture == Texture::band)
        {
            const double kick = 0.9 * std::sin (2 * pi * (50.0 * inBeat + 3.0 * (1 - std::exp (-inBeat * 40.0)))) * std::exp (-inBeat * 9.0);
            snare += 0.35 * (hash (static_cast<std::uint64_t> (clock) * 3 + 1) - snare);
            const double snap = beat % 2 == 1 ? 0.8 * std::exp (-inBeat * 22.0) * (snare + 0.4 * std::sin (2 * pi * 185.0 * inBeat)) : 0.0;
            const double noise = hash (static_cast<std::uint64_t> (clock) * 3 + 2);
            hat = 0.95 * (hat + noise - hatIn); hatIn = noise;
            const double hats = 0.45 * std::exp (-inEighth * 60.0) * hat;
            const double note = root * (beat % 4 == 3 ? 1.5 : 1.0);
            bass += 0.06 * (2.0 * std::fmod (t * note, 1.0) - 1.0 - bass);
            const double low = 0.45 * bass * (0.6 + 0.4 * std::exp (-inBeat * 4.0));
            bed = 0.985 * bed + 0.06 * hash (static_cast<std::uint64_t> (clock) * 7 + 11);
            const double room = 0.25 * (bed + 0.5 * hash (static_cast<std::uint64_t> (clock) * 7 + 12)) * (0.5 + 0.5 * std::exp (-inBeat * 3.0));
            const double guitar = 0.25 * std::tanh (5.0 * pad);
            left += kick + snap + low + hats + guitar + room;
            right += kick + snap + low + 0.8 * hats + 0.9 * guitar + room;
        }
        out[static_cast<std::size_t> (n) * 2] = static_cast<float> (0.55 * left);
        out[static_cast<std::size_t> (n) * 2 + 1] = static_cast<float> (0.55 * right);
    }
    return out;
}

// Seven bands: a 30 Hz high-pass, a low shelf, three peaks, a presence peak and a high shelf.
inline void equalize (std::vector<float>& x, double dB)
{
    for (int channel = 0; channel < 2; ++channel)
    {
        Biquad bands[] = { Biquad::make (0, 30, 0.707, 0), Biquad::make (2, 90, 0.707, dB), Biquad::make (1, 250, 1.0, -0.75 * dB),
                           Biquad::make (1, 1000, 1.4, 0.5 * dB), Biquad::make (1, 3000, 2.0, -dB),
                           Biquad::make (1, 6000, 1.0, 0.75 * dB), Biquad::make (3, 12000, 0.707, dB) };
        for (std::size_t i = static_cast<std::size_t> (channel); i < x.size(); i += 2)
        {
            double v = x[i];
            for (auto& band : bands) v = band.process (v);
            x[i] = static_cast<float> (v);
        }
    }
}

// Brickwall at -0.3 dBFS: no output sample above the ceiling. With lookahead the gain is the
// minimum ahead, smoothed over the lookahead, so it is down before the peak arrives; it recovers
// over 80 ms. The delay of lookahead - 1 samples is compensated, as the host does with its report.
// -100 dBFS dither follows. Returns the average gain reduction in dB.
inline double limit (std::vector<float>& x, double driveDb, int lookahead)
{
    const double drive = std::pow (10.0, driveDb / 20.0), ceiling = 0.966, release = 1.0 - std::exp (-1.0 / (0.08 * rate));
    const auto frames = x.size() / 2;
    const auto span = static_cast<std::size_t> (std::max (1, lookahead));
    std::vector<double> held (frames);
    std::deque<std::size_t> window;
    const auto target = [&] (std::size_t i)
    {
        const double peak = drive * std::max (std::fabs (x[i * 2]), std::fabs (x[i * 2 + 1]));
        return peak > ceiling ? ceiling / peak : 1.0;
    };
    std::vector<double> wanted (frames);
    for (std::size_t i = 0; i < frames; ++i) wanted[i] = target (i);
    for (std::size_t i = 0; i < frames; ++i)
    {
        while (! window.empty() && wanted[window.back()] >= wanted[i]) window.pop_back();
        window.push_back (i);
        if (window.front() + span <= i) window.pop_front();
        held[i] = wanted[window.front()];
    }
    double sum = 0.0, gain = 1.0, reduction = 0.0;
    std::vector<float> out (x.size());
    for (std::size_t i = 0; i < frames; ++i)
    {
        sum += held[i];
        if (i >= span) sum -= held[i - span];
        const double smooth = sum / static_cast<double> (std::min (span, i + 1));
        gain = smooth < gain ? smooth : gain + (smooth - gain) * release;
        const auto source = i >= span - 1 ? i - (span - 1) : 0;
        for (int channel = 0; channel < 2; ++channel)
            out[source * 2 + static_cast<std::size_t> (channel)] = static_cast<float> (gain * drive * x[source * 2 + static_cast<std::size_t> (channel)]);
        reduction += -20.0 * std::log10 (gain);
    }
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] += static_cast<float> (1.0e-5 * (hash (i * 5 + 3) + hash (i * 5 + 4)));
    x = std::move (out);
    return frames > 0 ? reduction / static_cast<double> (frames) : 0.0;
}

inline Tracks render (std::int64_t from, std::int64_t frames, Chain chain, Texture texture = Texture::band)
{
    Tracks tracks;
    tracks.from = from;
    tracks.pre = music (from, frames, texture);
    tracks.post = tracks.pre;
    if (chain.eqDb != 0.0) equalize (tracks.post, chain.eqDb);
    if (chain.driveDb > 0.0) tracks.averageReductionDb = limit (tracks.post, chain.driveDb, chain.lookahead);
    if (chain.trimDb != 0.0)
        for (auto& v : tracks.post) v = static_cast<float> (v * std::pow (10.0, chain.trimDb / 20.0));
    return tracks;
}
}
