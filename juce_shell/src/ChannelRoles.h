#pragma once

#include "AbiContract.h"
#include "kirin_hypha_channels.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>
#include <vector>

/**
    Maps a host's negotiated channel set onto Kirin's ITU role codes, and creates the engine from
    them.

    The count alone cannot say what a buffer holds — eight channels are 7.1 or 5.1.2 — so the shell
    reports what each channel *is* and lets the Rust side decide whether it knows that layout. A
    channel JUCE names something Kirin has no role for makes the whole set unrepresentable: the
    engine is not created rather than measured under a guess.
*/
namespace kirin
{

/** One JUCE channel type and the Kirin role it is.

    A table rather than a switch: the mapping is data, and JUCE's ChannelType carries ~80
    enumerators (ambisonic orders, discrete channels) that have no ITU role, which a switch would
    have to list one by one to stay warning-clean.
*/
struct ChannelRoleEntry
{
    juce::AudioChannelSet::ChannelType type;
    uint8_t role;
};

inline constexpr ChannelRoleEntry channelRoleTable[] = {
    { juce::AudioChannelSet::centre,            KIRIN_CHANNEL_ROLE_CENTRE },
    { juce::AudioChannelSet::left,              KIRIN_CHANNEL_ROLE_LEFT },
    { juce::AudioChannelSet::right,             KIRIN_CHANNEL_ROLE_RIGHT },
    { juce::AudioChannelSet::LFE,               KIRIN_CHANNEL_ROLE_LFE },
    { juce::AudioChannelSet::leftSurround,      KIRIN_CHANNEL_ROLE_LEFT_SURROUND },
    { juce::AudioChannelSet::rightSurround,     KIRIN_CHANNEL_ROLE_RIGHT_SURROUND },
    { juce::AudioChannelSet::leftSurroundSide,  KIRIN_CHANNEL_ROLE_LEFT_SURROUND_SIDE },
    { juce::AudioChannelSet::rightSurroundSide, KIRIN_CHANNEL_ROLE_RIGHT_SURROUND_SIDE },
    { juce::AudioChannelSet::leftSurroundRear,  KIRIN_CHANNEL_ROLE_LEFT_SURROUND_REAR },
    { juce::AudioChannelSet::rightSurroundRear, KIRIN_CHANNEL_ROLE_RIGHT_SURROUND_REAR },
    { juce::AudioChannelSet::topFrontLeft,      KIRIN_CHANNEL_ROLE_TOP_FRONT_LEFT },
    { juce::AudioChannelSet::topFrontRight,     KIRIN_CHANNEL_ROLE_TOP_FRONT_RIGHT },
    { juce::AudioChannelSet::topRearLeft,       KIRIN_CHANNEL_ROLE_TOP_REAR_LEFT },
    { juce::AudioChannelSet::topRearRight,      KIRIN_CHANNEL_ROLE_TOP_REAR_RIGHT },
};

/** The Kirin role code for one JUCE channel type, or -1 when there is no role for it. */
inline int channelRoleFor (juce::AudioChannelSet::ChannelType type) noexcept
{
    for (const auto& entry : channelRoleTable)
        if (entry.type == type)
            return (int) entry.role;
    return -1;
}

/** The role codes for a channel set, in buffer order. Empty when any channel has no role. */
inline std::vector<uint8_t> channelRoles (const juce::AudioChannelSet& set)
{
    std::vector<uint8_t> roles;
    roles.reserve ((size_t) set.size());
    for (int i = 0; i < set.size(); ++i)
    {
        const int role = channelRoleFor (set.getTypeOfChannel (i));
        if (role < 0)
            return {};
        roles.push_back ((uint8_t) role);
    }
    return roles;
}

inline bool isMonoRoles (const std::vector<uint8_t>& roles) noexcept
{
    return roles.size() == 1u && roles[0] == KIRIN_CHANNEL_ROLE_CENTRE;
}

inline bool isStereoRoles (const std::vector<uint8_t>& roles) noexcept
{
    return roles.size() == 2u && roles[0] == KIRIN_CHANNEL_ROLE_LEFT
           && roles[1] == KIRIN_CHANNEL_ROLE_RIGHT;
}

inline bool isSurround51Roles (const std::vector<uint8_t>& roles) noexcept
{
    return roles.size() == 6u && roles[0] == KIRIN_CHANNEL_ROLE_LEFT
           && roles[1] == KIRIN_CHANNEL_ROLE_RIGHT && roles[2] == KIRIN_CHANNEL_ROLE_CENTRE
           && roles[3] == KIRIN_CHANNEL_ROLE_LFE
           && roles[4] == KIRIN_CHANNEL_ROLE_LEFT_SURROUND
           && roles[5] == KIRIN_CHANNEL_ROLE_RIGHT_SURROUND;
}

inline bool supportsStereoWorkflows (const std::vector<uint8_t>& roles) noexcept
{
    return isMonoRoles (roles) || isStereoRoles (roles);
}

inline const char* channelRoleShortName (uint8_t role) noexcept
{
    switch (role)
    {
        case KIRIN_CHANNEL_ROLE_CENTRE:               return "C";
        case KIRIN_CHANNEL_ROLE_LEFT:                 return "L";
        case KIRIN_CHANNEL_ROLE_RIGHT:                return "R";
        case KIRIN_CHANNEL_ROLE_LFE:                  return "LFE";
        case KIRIN_CHANNEL_ROLE_LEFT_SURROUND:        return "Ls";
        case KIRIN_CHANNEL_ROLE_RIGHT_SURROUND:       return "Rs";
        case KIRIN_CHANNEL_ROLE_LEFT_SURROUND_SIDE:   return "Lss";
        case KIRIN_CHANNEL_ROLE_RIGHT_SURROUND_SIDE:  return "Rss";
        case KIRIN_CHANNEL_ROLE_LEFT_SURROUND_REAR:   return "Lrs";
        case KIRIN_CHANNEL_ROLE_RIGHT_SURROUND_REAR:  return "Rrs";
        case KIRIN_CHANNEL_ROLE_TOP_FRONT_LEFT:       return "Tfl";
        case KIRIN_CHANNEL_ROLE_TOP_FRONT_RIGHT:      return "Tfr";
        case KIRIN_CHANNEL_ROLE_TOP_REAR_LEFT:        return "Trl";
        case KIRIN_CHANNEL_ROLE_TOP_REAR_RIGHT:       return "Trr";
        default:                                      return "?";
    }
}

/** Creates an engine for a role list, or returns nullptr when it is not measurable. */
inline KirinHypha* createEngineForRoles (double sampleRate, const std::vector<uint8_t>& roles)
{
    // A library built against a different ABI reads every field at the wrong offset, so nothing it
    // produces is a measurement. Refuse before the first sample rather than display it.
    if (! abiMatchesLinkedLibraryOnce() || roles.empty())
        return nullptr;
    return kirin_hypha_create ((uint32_t) sampleRate, roles.data(), (uint32_t) roles.size());
}

} // namespace kirin
