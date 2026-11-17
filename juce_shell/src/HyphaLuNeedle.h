#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// The LU face of the Hybrid VU: loudness read against a Reference song from Kirin OS.
// 0 LU is the loudness of the song chosen in Reference B or C; there is no numeric target.
// Everything here is display arithmetic on values that are already measured.
namespace hypha::lu_needle
{
constexpr double scaleLowLu = -18.0;
constexpr double scaleHighLu = 9.0;
constexpr double nan = std::numeric_limits<double>::quiet_NaN();

// Which loudness the needles read: Momentary (0.4 s), Short-term (3 s) or Integrated.
enum class Window : std::uint8_t { momentary, shortTerm, integrated };

constexpr const char* windowLabel (Window window) noexcept
{
    return window == Window::momentary ? "M" : window == Window::shortTerm ? "S" : "I";
}

constexpr Window nextWindow (Window window) noexcept
{
    return window == Window::momentary ? Window::shortTerm
         : window == Window::shortTerm ? Window::integrated : Window::momentary;
}

// The face and window as one saved value: bit 0 is the LU face, bits 1-2 the window.
constexpr std::uint8_t packChoice (bool luFace, Window window) noexcept
{
    return static_cast<std::uint8_t> ((luFace ? 1u : 0u) | (static_cast<unsigned> (window) << 1));
}
constexpr bool packedLuFace (std::uint8_t packed) noexcept { return (packed & 1u) != 0; }
constexpr Window packedWindow (std::uint8_t packed) noexcept
{
    const auto value = (packed >> 1) & 3u;
    return value == 1u ? Window::shortTerm : value == 2u ? Window::integrated : Window::momentary;
}

// The scale is linear from -18 to +9 LU (the EBU +9 scale). A value that does not exist rests
// on the left stop; values past either end rest on that end.
inline float normalized (double lu) noexcept
{
    if (! std::isfinite (lu)) return 0.0f;
    return static_cast<float> ((std::clamp (lu, scaleLowLu, scaleHighLu) - scaleLowLu)
                               / (scaleHighLu - scaleLowLu));
}

// LUFS against the 0 of the face. Either side missing gives no value.
inline double relativeLu (double lufs, double zeroLufs) noexcept
{
    return std::isfinite (lufs) && std::isfinite (zeroLufs) ? lufs - zeroLufs : nan;
}

struct Band
{
    double lowLufs = nan, highLufs = nan;
    bool valid() const noexcept { return std::isfinite (lowLufs) && std::isfinite (highLufs); }
    bool operator== (const Band& other) const noexcept
    {
        return valid() == other.valid() && (! valid() || (lowLufs == other.lowLufs && highLufs == other.highLufs));
    }
};

// p10 to p90 (nearest rank, as Kirin OS orders a Cue's values) of the songs that have a value.
// One song is not a range: fewer than two values give no band.
inline Band percentileBand (std::vector<double> values)
{
    values.erase (std::remove_if (values.begin(), values.end(),
                                  [] (double value) { return ! std::isfinite (value); }), values.end());
    if (values.size() < 2) return {};
    std::sort (values.begin(), values.end());
    const auto rank = [&values] (double fraction)
    {
        const auto index = static_cast<std::size_t> (std::ceil (fraction * static_cast<double> (values.size())));
        return values[std::clamp<std::size_t> (index, 1, values.size()) - 1];
    };
    return { rank (0.10), rank (0.90) };
}

enum class Zero : std::uint8_t
{
    noSong,   // no Reference song is chosen in B or C
    noValue,  // the chosen song has no loudness from Kirin OS
    ready,
};

enum class SamePlace : std::uint8_t
{
    off,        // not FOLLOW A, LOOP, Blind, or the Integrated window
    shown,
    noValueHere // FOLLOW A, but Kirin OS has no value at this position (outside the Cue or the song)
};

// What Reference gives the face. POST builds it from its Reference state; PRE reads the copy POST
// shares for the same chain.
struct Facts
{
    bool readable = false;        // the Kirin OS Reference library can be read
    Zero zero = Zero::noSong;
    juce::String song;
    double zeroLufs = nan;        // Kirin OS's value for the song, as its lists show it
    double playedOffsetLu = 0.0;  // what the played song measures above that value (mono on stereo)
    Band band;                    // the chosen set's songs, as played
    SamePlace samePlace = SamePlace::off;
    double samePlaceLufs = nan;   // the song at A's position, as played

    double playedZero() const noexcept { return zero == Zero::ready ? zeroLufs + playedOffsetLu : nan; }
    bool operator== (const Facts& other) const noexcept
    {
        const auto same = [] (double a, double b) { return a == b || (std::isnan (a) && std::isnan (b)); };
        return readable == other.readable && zero == other.zero && song == other.song
            && same (zeroLufs, other.zeroLufs) && playedOffsetLu == other.playedOffsetLu && band == other.band
            && samePlace == other.samePlace && same (samePlaceLufs, other.samePlaceLufs);
    }
    bool operator!= (const Facts& other) const noexcept { return ! (*this == other); }
};

// The needles of one paint, in LU against the played 0. A missing value is NaN.
struct Reading
{
    double own = nan;        // this plug-in's signal (POST in POST, PRE in PRE)
    double pre = nan;        // POST only: the PRE of the same moment, when it was confirmed
    double samePlace = nan;  // the Reference song at the same place (FOLLOW A)
    double postMinusPre = nan;
};

// own: the chosen window of this plug-in. postMinusPre: the confirmed POST - PRE of the same
// window, or NaN when there is none (PRE itself, no pair, no confirmed moment, the Integrated window).
inline Reading reading (const Facts& facts, Window window, double ownLufs, double postMinusPre) noexcept
{
    Reading result;
    const auto zero = facts.readable ? facts.playedZero() : nan;
    result.own = relativeLu (ownLufs, zero);
    if (window != Window::integrated && std::isfinite (postMinusPre) && std::isfinite (result.own))
    {
        result.pre = result.own - postMinusPre;
        result.postMinusPre = postMinusPre;
    }
    if (window != Window::integrated && facts.samePlace == SamePlace::shown)
        result.samePlace = relativeLu (facts.samePlaceLufs, zero);
    return result;
}

// The needle points at the chosen value itself: no 300 ms VU ballistics. Only a short ease
// between two publications (10 per second) keeps it from stepping: a 60 ms time constant, so the
// needle is within 5 % of a new value after 180 ms and never lags a 400 ms window visibly.
constexpr double easeSeconds = 0.060;

struct EasedNeedle
{
    float shown = 0.0f;
    void snap (float target) noexcept { shown = target; }
    // Returns true while the needle still moves.
    bool step (float target, double seconds) noexcept
    {
        if (! (seconds > 0.0)) return shown != target;
        const auto next = shown + (target - shown) * static_cast<float> (1.0 - std::exp (-seconds / easeSeconds));
        shown = std::abs (target - next) < 0.0008f ? target : next;
        return shown != target;
    }
};

inline juce::String signedLu (double lu)
{
    if (! std::isfinite (lu)) return "---";
    const auto rounded = std::round (lu * 10.0) / 10.0;
    return (rounded > 0.0 ? "+" : rounded < 0.0 ? "-" : "") + juce::String (std::abs (rounded), 1);
}
}
