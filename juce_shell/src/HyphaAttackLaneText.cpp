#include "HyphaAttackLanePainter.h"

#include <cmath>

#include "HyphaAttackUiContract.h"
#include "HyphaTheme.h"

// The words and numbers of the DRUM lanes: names, codes, scales, units, values and the reasons
// a value is withheld. Labels stay English; the reasons have Japanese in the catalog (INV-S40).
namespace hypha::attack_lane_painter
{
using attack_lanes::index;
using attack_lanes::Lane;
using attack_lanes::Reason;

juce::Colour colourFor (Lane lane) noexcept
{
    // The band lanes take the same four colours in row order, so the rows keep their identity.
    switch (index (lane))
    {
        case 0: return juce::Colour (attack_ui::transientColour);
        case 1: return juce::Colour (attack_ui::strengthColour);
        case 2: return juce::Colour (attack_ui::crestColour);
        case 3: return juce::Colour (attack_ui::sharpnessColour);
    }
    return COL_NORMAL;
}

juce::String nameFor (Lane lane)
{
    constexpr const char* names[] { "TRANSIENT", "STRENGTH", "CREST", "SHARPNESS",
                                    "DELAY", "ATT", "REL", "LEVEL" };
    return names[static_cast<std::size_t> (lane)];
}

juce::String codeFor (Lane lane)
{
    constexpr const char* codes[] { "TR", "ST", "CR", "SH", "DL", "AT", "RL", "LV" };
    return codes[static_cast<std::size_t> (lane)];
}

juce::String scaleCaption (Lane lane, bool delta)
{
    if (attack_lanes::isBand (lane))
    {
        constexpr const char* band[] { "+/-10 ms", "+/-10 ms", "+/-100 ms", "+/-12 dB" };
        constexpr const char* absolute[] { "+/-10 ms", "0..40 ms", "0..300 ms", "-72..0 dBFS" };
        return (delta ? band : absolute)[index (lane)];
    }
    if (delta)
        return lane == Lane::sharpness ? "+/-1 acum" : "+/-12 dB";
    constexpr const char* absolute[] { "-12..24 dB", "-72..0 dBFS", "0..24 dB", "0..8 acum" };
    return absolute[index (lane)];
}

juce::String unitFor (Lane lane, bool delta)
{
    switch (lane)
    {
        case Lane::transient:
        case Lane::crest:      return "dB";
        case Lane::sharpness:  return "acum";
        case Lane::delay:
        case Lane::attackTime:
        case Lane::release:    return "ms";
        case Lane::strength:
        case Lane::level:      return delta ? "dB" : "dBFS";
    }
    return "dB";
}

juce::String valueText (Lane lane, float value, bool delta, bool withUnit)
{
    if (! std::isfinite (value))
        return "--";
    const auto decimals = lane == Lane::sharpness ? 2 : lane == Lane::release ? 0 : 1;
    const auto step = std::pow (10.0f, static_cast<float> (decimals));
    auto rounded = std::round (value * step) / step;
    if (rounded == 0.0f)
        rounded = 0.0f; // never print -0.0
    auto text = juce::String (rounded, decimals);
    if (delta && rounded >= 0.0f)
        text = "+" + text;
    return withUnit ? text + " " + unitFor (lane, delta) : text;
}

juce::String resolutionText (float ms)
{
    if (ms >= 1.0f)
        return juce::String (juce::roundToInt (ms)) + " ms";
    // Rounded here, half up: the 8 kHz period 0.125 ms reads 0.13, not printf's 0.12.
    const auto decimals = ms >= 0.3f ? 1 : 2;
    const auto step = decimals == 1 ? 10.0f : 100.0f;
    return juce::String (std::round (ms * step) / step, decimals) + " ms";
}

juce::String boundText (float ms)
{
    return "<" + resolutionText (ms);
}

juce::String reasonText (const attack_lanes::Hit& hit, Reason reason)
{
    switch (reason)
    {
        case Reason::value:
        case Reason::belowResolution: return {};
        case Reason::missing:         return "--";
        case Reason::nextHit:         return "NEXT HIT";
        case Reason::quietBody:       return "QUIET AFTER";
        case Reason::ringing:         return "RINGING";
        case Reason::noPreBand:       return "PRE NO BAND";
        case Reason::noMatch:
            return hit.pre.available && ! hit.post.available ? "PRE ONLY"
                 : hit.post.available && ! hit.pre.available ? "POST ONLY" : "NO PAIR";
    }
    return "--";
}

juce::String cellText (const attack_lanes::Hit& hit, Lane lane, bool delta, bool withUnit)
{
    const auto& cell = hit.cells[index (lane)];
    if (cell.reason == Reason::value)
        return valueText (lane, cell.value, delta, withUnit);
    // ATT within the band's resolution: the bound, never a finer number (D4).
    if (cell.reason == Reason::belowResolution)
        return withUnit ? boundText (cell.value) : boundText (cell.value).upToFirstOccurrenceOf (" ", false, false);
    return reasonText (hit, cell.reason);
}
}
