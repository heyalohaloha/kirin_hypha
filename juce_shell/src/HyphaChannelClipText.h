#pragma once

#include "ChannelRoles.h"
#include "kirin_hypha_ffi.h"

#include <algorithm>
#include <cstddef>

namespace hypha::channel_clip
{
inline std::size_t count (const KirinMeterSession* meter) noexcept
{
    return meter != nullptr && meter->channels > 0
        ? std::min<std::size_t> (meter->channels, KIRIN_MAX_CHANNELS) : 2u;
}

inline const char* role (const KirinMeterSession* meter, std::size_t channel) noexcept
{
    if (meter == nullptr || channel >= count (meter))
        return channel == 0u ? "L" : channel == 1u ? "R" : "?";
    return kirin::channelRoleShortName (meter->channel_positions[channel]);
}

template <typename Counts>
std::uint64_t total (const Counts& counts, const KirinMeterSession* meter) noexcept
{
    std::uint64_t result = 0;
    for (std::size_t channel = 0; channel < count (meter); ++channel)
        result += counts[channel];
    return result;
}

template <typename Counts>
juce::String text (const Counts& counts, const KirinMeterSession* meter, bool nonzeroOnly)
{
    juce::String result ("CLIP");
    for (std::size_t channel = 0; channel < count (meter); ++channel)
    {
        if (nonzeroOnly && counts[channel] == 0u) continue;
        result += " " + juce::String (role (meter, channel))
                + juce::String (counts[channel]);
    }
    return result == "CLIP" ? juce::String ("CLIP 0") : result;
}
} // namespace hypha::channel_clip
