#pragma once

#include <cstdint>
#include <optional>

#include <juce_core/juce_core.h>

namespace hypha::appearance
{
inline constexpr auto activationSchemaVersion = "1.1.0";
inline constexpr auto preferenceSchemaVersion = "1.0.0";
inline constexpr auto activationKind = "kirin_os_jungle_activation";
inline constexpr auto themeId = "ce2226";
inline constexpr std::int64_t maxDocumentBytes = 4096;

enum class Choice { unset, on, off };
enum class DecodeState { valid, invalid, futureSchema };

struct Activation final
{
    juce::String id;
    juce::String activatedAt;
    juce::String jungleActivatedAt;
    juce::String maskingGuideId;
    juce::String maskingSentAt;
};

struct Preference final
{
    std::int64_t revision = 0;
    bool activationSeen = false;
    juce::String firstActivationId;
    Choice choice = Choice::unset;
    bool noticeAcknowledged = false;
};

struct ActivationDecode final
{
    DecodeState state = DecodeState::invalid;
    std::optional<Activation> value;
};

struct PreferenceDecode final
{
    DecodeState state = DecodeState::invalid;
    std::optional<Preference> value;
};

struct Snapshot final
{
    std::uint64_t generation = 0;
    std::int64_t revision = 0;
    bool activationSeen = false;
    bool enabled = false;
    bool noticePending = false;
    Choice choice = Choice::unset;
    bool persistent = true;

    bool operator== (const Snapshot& other) const noexcept
    {
        return generation == other.generation && revision == other.revision
            && activationSeen == other.activationSeen && enabled == other.enabled
            && noticePending == other.noticePending && choice == other.choice
            && persistent == other.persistent;
    }
};

ActivationDecode decodeActivation (const juce::String&);
PreferenceDecode decodePreference (const juce::String&);
juce::String encodePreference (const Preference&);
Preference defaultPreference();
Snapshot makeSnapshot (const Preference&, bool persistent, std::uint64_t generation = 0);
const char* choiceName (Choice);
std::optional<Choice> choiceFromName (const juce::String&);
}
