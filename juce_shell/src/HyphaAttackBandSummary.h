#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <juce_core/juce_core.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackLaneModel.h"

// DRUM band summary (2026-09-29): the words and numbers of the recent hits that rise in the
// chosen band, as the engine summed them (KirinAttackBandSummary). The view says what the chain
// did to them in one reading: each lane's median with its direction in words and how many hits
// agree, a median inside what the band can tell apart as "SAME", and POST's own values when no
// PRE compares. Labels and units stay English; the sentences have Japanese (INV-S40).
namespace hypha::attack_band_summary
{
// DELAY, ATT, REL, LEVEL: the order of the engine's lanes and of attack_lanes::bandLanes.
constexpr std::size_t laneCount = 4;

static_assert (attack_lanes::bandLanes[0] == attack_lanes::Lane::delay
               && attack_lanes::bandLanes[1] == attack_lanes::Lane::attackTime
               && attack_lanes::bandLanes[2] == attack_lanes::Lane::release
               && attack_lanes::bandLanes[3] == attack_lanes::Lane::level);

// The engine's layout (attack_ffi_band_summary.rs summary_c_layout_is_fixed).
static_assert (sizeof (KirinAttackBandLaneSummary) == 52);
static_assert (offsetof (KirinAttackBandLaneSummary, withheld) == 3);
static_assert (offsetof (KirinAttackBandSummary, lanes) == 96);
static_assert (sizeof (KirinAttackBandSummary) == 320 + 4 * 640);

struct Scale
{
    float from = 0.0f;
    float to = 1.0f;
    bool none = false; // DELAY without PRE has no scale
};

// The band lane's own scale, as its label states it (attack_lanes::scaleFor): POST - PRE symmetric
// about zero, POST's own values from the value's natural floor.
inline Scale scaleFor (std::size_t lane, bool delta) noexcept
{
    const auto scale = attack_lanes::scaleFor (attack_lanes::bandLanes[lane < laneCount ? lane : 0], delta);
    return { scale.minimum, scale.maximum, lane == 0 && ! delta };
}

inline juce::String unitFor (std::size_t lane, bool delta)
{
    return lane == 3 ? juce::String (delta ? "dB" : "dBFS") : juce::String ("ms");
}

inline int decimalsFor (std::size_t lane) noexcept
{
    return lane == 2 ? 0 : 1;
}

inline juce::String number (float value, int decimals, bool sign)
{
    const auto step = std::pow (10.0f, static_cast<float> (decimals));
    auto rounded = std::round (value * step) / step;
    if (rounded == 0.0f)
        rounded = 0.0f;
    const auto text = juce::String (std::abs (rounded), decimals);
    if (! sign)
        return rounded < 0.0f ? "-" + text : text;
    return (rounded < 0.0f ? "-" : "+") + text;
}

// A resolution as it reads: "16 ms", "0.5 ms", "0.13 ms".
inline juce::String resolutionText (float ms)
{
    return number (ms, ms >= 1.0f ? 0 : ms >= 0.3f ? 1 : 2, false) + " ms";
}

inline bool delta (const KirinAttackBandSummary& summary) noexcept
{
    return summary.delta != 0;
}

inline const KirinAttackBandLaneSummary& laneOf (const KirinAttackBandSummary& summary,
                                                 std::size_t lane) noexcept
{
    return summary.lanes[lane < laneCount ? lane : 0];
}

// The lane's direction for a POST - PRE median: LATER / EARLIER and the like.
inline juce::String directionWord (std::size_t lane, float median)
{
    constexpr const char* more[laneCount] { "LATER", "SLOWER", "LONGER", "LOUDER" };
    constexpr const char* less[laneCount] { "EARLIER", "FASTER", "SHORTER", "QUIETER" };
    return median > 0.0f ? more[lane] : less[lane];
}

// The large text of a lane: "+2.4 ms", "SAME", "<16 ms", "127 ms" or "--".
inline juce::String valueText (const KirinAttackBandSummary& summary, std::size_t lane, bool withUnit = true)
{
    const auto& entry = laneOf (summary, lane);
    const auto unit = withUnit ? " " + unitFor (lane, delta (summary)) : juce::String();
    switch (entry.state)
    {
        case KIRIN_ATTACK_BAND_LANE_VALUE:
            return number (entry.median, decimalsFor (lane), delta (summary)) + unit;
        case KIRIN_ATTACK_BAND_LANE_WITHIN:
            return delta (summary) ? juce::String ("SAME")
                                   : "<" + (withUnit ? resolutionText (entry.within)
                                                     : resolutionText (entry.within).upToFirstOccurrenceOf (" ", false, false));
        default:
            return "--";
    }
}

// Why a lane without a value has none: DELAY without PRE says `delayReason` (NO PAIR, UPDATE PRE);
// otherwise the reason the engine found most among the summed hits, or "--" while nothing is summed.
inline juce::String withheldText (const KirinAttackBandSummary& summary, std::size_t lane,
                                  const juce::String& delayReason)
{
    if (lane == 0 && ! delta (summary))
        return delayReason;
    switch (laneOf (summary, lane).withheld)
    {
        case KIRIN_ATTACK_BAND_HELD_RINGING:   return "RINGING";
        case KIRIN_ATTACK_BAND_HELD_NEXT_HIT:  return "NEXT HIT";
        case KIRIN_ATTACK_BAND_HELD_LONG_TAIL: return "LONG TAIL";
        default:                               return "--";
    }
}

// A lane's text as shown: its value, or why it has none.
inline juce::String shownText (const KirinAttackBandSummary& summary, std::size_t lane,
                               const juce::String& delayReason, bool withUnit = true)
{
    return laneOf (summary, lane).state == KIRIN_ATTACK_BAND_LANE_NONE ? withheldText (summary, lane, delayReason)
                                                                       : valueText (summary, lane, withUnit);
}

// The line under it: "LATER  8/8" for a stated difference, "WITHIN 16 ms" for SAME, nothing else.
inline juce::String wordText (const KirinAttackBandSummary& summary, std::size_t lane)
{
    const auto& entry = laneOf (summary, lane);
    if (! delta (summary))
        return {};
    if (entry.state == KIRIN_ATTACK_BAND_LANE_WITHIN)
        return "WITHIN " + resolutionText (entry.within);
    if (entry.state == KIRIN_ATTACK_BAND_LANE_VALUE)
        return directionWord (lane, entry.median);
    return {};
}

inline juce::String agreeText (const KirinAttackBandSummary& summary, std::size_t lane)
{
    const auto& entry = laneOf (summary, lane);
    if (! delta (summary) || entry.state != KIRIN_ATTACK_BAND_LANE_VALUE)
        return {};
    return juce::String (static_cast<int> (entry.agree)) + "/" + juce::String (static_cast<int> (entry.count));
}

// "LAST 8 HITS", "LAST HIT": what the summary sums.
inline juce::String lastText (std::uint32_t count)
{
    return count == 1 ? juce::String ("LAST HIT") : "LAST " + juce::String (static_cast<int> (count)) + " HITS";
}

// "500 Hz  LAST 8 HITS"; the band alone while nothing is summed.
inline juce::String titleText (const juce::String& bandName, std::uint32_t count)
{
    return count == 0 ? bandName : bandName + "  " + lastText (count);
}

inline juce::String leftOutText (const juce::String& bandName, std::uint32_t leftOut)
{
    return leftOut == 1 ? "1 HIT WITHOUT " + bandName + " LEFT OUT"
                        : juce::String (static_cast<int> (leftOut)) + " HITS WITHOUT " + bandName + " LEFT OUT";
}

// One fact of the reading, and the lane it is about. `brief` says it where the full sentence does
// not fit: each direction word belongs to one lane, so "LATER 2.7 ms" needs no subject; `bare` is
// the lane's value or state alone, beside its lane's colour. `quiet` is a lane that did not move or
// has no value; it is written after the ones that moved.
struct Fact
{
    juce::String text;
    juce::String brief;
    juce::String bare;
    std::size_t lane = 0;
    bool quiet = false;
};

// One line a fact, what moved first and what did not after. POST's own values when nothing is
// compared; `delayReason` says why DELAY has none then (NO PAIR, UPDATE PRE).
inline std::vector<Fact> cardFacts (const KirinAttackBandSummary& summary, const juce::String& delayReason)
{
    std::vector<Fact> moved, still;
    constexpr const char* subject[laneCount] { "POST", "ATTACK", "TAIL", "LEVEL" };
    constexpr const char* topic[laneCount] { "DELAY", "ATTACK", "TAIL", "LEVEL" };
    constexpr const char* name[laneCount] { "DELAY", "ATT", "REL", "LEVEL" };
    for (std::size_t lane = 0; lane < laneCount; ++lane)
    {
        const auto& entry = laneOf (summary, lane);
        if (entry.state == KIRIN_ATTACK_BAND_LANE_NONE)
        {
            // "TAIL: NEXT HIT": no summed hit has the value, and why.
            if (const auto why = withheldText (summary, lane, delayReason); why != "--")
            {
                const auto text = juce::String (topic[lane]) + ": " + why;
                still.push_back ({ text, text, why, lane, true });
            }
        }
        else if (! delta (summary))
        {
            const auto value = valueText (summary, lane);
            moved.push_back ({ juce::String (name[lane]) + " " + value, juce::String (name[lane]) + " " + value, value,
                               lane, false });
        }
        else if (entry.state == KIRIN_ATTACK_BAND_LANE_VALUE)
        {
            const auto amount = number (std::abs (entry.median), decimalsFor (lane), false) + " " + unitFor (lane, true);
            const auto word = directionWord (lane, entry.median);
            moved.push_back ({ juce::String (subject[lane]) + " " + amount + " " + word, word + " " + amount,
                               valueText (summary, lane), lane, false });
        }
        else
        {
            const auto text = juce::String (topic[lane]) + ": SAME";
            still.push_back ({ text, text, "SAME", lane, true });
        }
    }
    moved.insert (moved.end(), still.begin(), still.end());
    return moved;
}

// The summary changed in anything drawn (the engine polls it every presentation tick).
inline bool same (const KirinAttackBandSummary& a, const KirinAttackBandSummary& b) noexcept
{
    return std::memcmp (&a, &b, sizeof (KirinAttackBandSummary)) == 0;
}
}
