#pragma once
#include <cstdint>
namespace hypha::live_compare
{
// Non-RT host inspection and UI guidance only; none of these values authorises audio.
enum class ReaperChainRestriction : std::uint8_t { none, unavailable, channels, inputFx, pdcMode, routing };
inline const char* reaperRestrictionText (ReaperChainRestriction reason) noexcept
{
    switch (reason)
    {
        case ReaperChainRestriction::channels: return "Loop needs 2 track channels (POST)";
        case ReaperChainRestriction::inputFx: return "Loop cannot verify input FX (POST)";
        case ReaperChainRestriction::pdcMode: return "Loop needs default PDC mode (POST)";
        case ReaperChainRestriction::routing: return "Loop needs a plain serial chain (POST)";
        case ReaperChainRestriction::unavailable: return "Track setup cannot verify the loop; turn loop off (POST)";
        case ReaperChainRestriction::none: return "";
    }
    return "";
}
// A stopped or unrelated instance never keeps the host API inspection at 20 Hz.
inline bool reaperMonitorRequired (bool enabled, bool interested, bool playing) noexcept
{ return enabled && interested && playing; }
}
