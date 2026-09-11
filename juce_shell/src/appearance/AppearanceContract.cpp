#include "AppearanceContract.h"

#include <array>

namespace hypha::appearance
{
namespace
{
constexpr std::int64_t maxSafeJsonInteger = 9'007'199'254'740'991LL;

bool isHex (juce::juce_wchar c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isUuid (const juce::String& text)
{
    if (text.length() != 36 || text[8] != '-' || text[13] != '-'
        || text[18] != '-' || text[23] != '-')
        return false;
    for (int index = 0; index < text.length(); ++index)
        if (index != 8 && index != 13 && index != 18 && index != 23 && ! isHex (text[index]))
            return false;
    const auto version = text[14];
    const auto variant = text[19];
    return version >= '1' && version <= '8'
        && (variant == '8' || variant == '9' || variant == 'a' || variant == 'A'
            || variant == 'b' || variant == 'B');
}

bool isCanonicalUtc (const juce::String& text)
{
    if (text.length() != 24 || text[4] != '-' || text[7] != '-' || text[10] != 'T'
        || text[13] != ':' || text[16] != ':' || text[19] != '.' || text[23] != 'Z')
        return false;
    for (int index = 0; index < text.length(); ++index)
        if (index != 4 && index != 7 && index != 10 && index != 13 && index != 16
            && index != 19 && index != 23 && (text[index] < '0' || text[index] > '9'))
            return false;
    const int year = text.substring (0, 4).getIntValue();
    const int month = text.substring (5, 7).getIntValue();
    const int day = text.substring (8, 10).getIntValue();
    const int hours = text.substring (11, 13).getIntValue();
    const int minutes = text.substring (14, 16).getIntValue();
    const int seconds = text.substring (17, 19).getIntValue();
    constexpr int daysByMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (year < 1 || month < 1 || month > 12 || hours > 23 || minutes > 59 || seconds > 59)
        return false;
    int days = daysByMonth[month - 1];
    if (month == 2 && (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0)))
        ++days;
    return day >= 1 && day <= days;
}

bool hasExactKeys (juce::DynamicObject& object, const std::initializer_list<const char*>& keys)
{
    const auto& properties = object.getProperties();
    if (properties.size() != static_cast<int> (keys.size()))
        return false;
    for (const auto* key : keys)
        if (! object.hasProperty (juce::Identifier (key)))
            return false;
    return true;
}

std::optional<std::array<std::int64_t, 3>> schemaParts (const juce::String& text)
{
    juce::StringArray parts;
    parts.addTokens (text, ".", {});
    if (parts.size() != 3)
        return std::nullopt;
    std::array<std::int64_t, 3> result {};
    for (int index = 0; index < 3; ++index)
    {
        if (parts[index].isEmpty()
            || parts[index].containsAnyOf ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+-"))
            return std::nullopt;
        for (const auto character : parts[index])
            if (character < '0' || character > '9')
                return std::nullopt;
        result[(size_t) index] = parts[index].getLargeIntValue();
    }
    return result;
}

bool hasFutureSchema (const juce::var& document, const char* supported)
{
    auto* object = document.getDynamicObject();
    if (object == nullptr)
        return false;
    const auto found = schemaParts (object->getProperty ("schema_version").toString());
    const auto current = schemaParts (supported);
    return found.has_value() && current.has_value() && *found > *current;
}

bool decodeBoolean (const juce::var& value, bool& output)
{
    if (! value.isBool())
        return false;
    output = static_cast<bool> (value);
    return true;
}
}

const char* choiceName (Choice choice)
{
    switch (choice)
    {
        case Choice::unset: return "unset";
        case Choice::on: return "on";
        case Choice::off: return "off";
    }
    return "unset";
}

std::optional<Choice> choiceFromName (const juce::String& text)
{
    if (text == "unset") return Choice::unset;
    if (text == "on") return Choice::on;
    if (text == "off") return Choice::off;
    return std::nullopt;
}

ActivationDecode decodeActivation (const juce::String& text)
{
    if (text.getNumBytesAsUTF8() > maxDocumentBytes)
        return {};
    const auto document = juce::JSON::parse (text);
    auto* object = document.getDynamicObject();
    if (object == nullptr)
        return {};
    if (hasFutureSchema (document, activationSchemaVersion))
        return { DecodeState::futureSchema, std::nullopt };
    if (! hasExactKeys (*object,
            { "schema_version", "kind", "activation_id", "activated_at",
              "jungle_activated_at", "masking_guide_id", "masking_sent_at", "theme" }))
        return {};
    const auto id = object->getProperty ("activation_id").toString();
    const auto activatedAt = object->getProperty ("activated_at").toString();
    const auto jungleActivatedAt = object->getProperty ("jungle_activated_at").toString();
    const auto maskingGuideId = object->getProperty ("masking_guide_id").toString();
    const auto maskingSentAt = object->getProperty ("masking_sent_at").toString();
    if (object->getProperty ("schema_version").toString() != activationSchemaVersion
        || object->getProperty ("kind").toString() != activationKind
        || object->getProperty ("theme").toString() != themeId
        || ! isUuid (id) || ! isUuid (maskingGuideId)
        || ! isCanonicalUtc (activatedAt) || ! isCanonicalUtc (jungleActivatedAt)
        || ! isCanonicalUtc (maskingSentAt)
        || activatedAt != juce::jmax (jungleActivatedAt, maskingSentAt))
        return {};
    return { DecodeState::valid,
        Activation { id, activatedAt, jungleActivatedAt, maskingGuideId, maskingSentAt } };
}

PreferenceDecode decodePreference (const juce::String& text)
{
    if (text.getNumBytesAsUTF8() > maxDocumentBytes)
        return {};
    const auto document = juce::JSON::parse (text);
    auto* object = document.getDynamicObject();
    if (object == nullptr)
        return {};
    if (hasFutureSchema (document, preferenceSchemaVersion))
        return { DecodeState::futureSchema, std::nullopt };
    if (! hasExactKeys (*object, { "schema_version", "revision", "activation_seen",
                                  "first_activation_id", "choice", "notice_acknowledged" }))
        return {};
    const auto revisionValue = object->getProperty ("revision");
    const auto revision = static_cast<std::int64_t> (revisionValue);
    bool activationSeen = false;
    bool noticeAcknowledged = false;
    const auto choice = choiceFromName (object->getProperty ("choice").toString());
    const auto idValue = object->getProperty ("first_activation_id");
    const bool nullId = idValue.isVoid() || idValue.isUndefined();
    const auto id = nullId ? juce::String() : idValue.toString();
    if (object->getProperty ("schema_version").toString() != preferenceSchemaVersion
        || (! revisionValue.isInt() && ! revisionValue.isInt64())
        || revision < 0 || revision > maxSafeJsonInteger
        || ! decodeBoolean (object->getProperty ("activation_seen"), activationSeen)
        || ! decodeBoolean (object->getProperty ("notice_acknowledged"), noticeAcknowledged)
        || ! choice.has_value() || (! nullId && ! isUuid (id))
        || activationSeen != ! nullId)
        return {};
    return { DecodeState::valid,
             Preference { revision, activationSeen, id, *choice, noticeAcknowledged } };
}

juce::String encodePreference (const Preference& preference)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("schema_version", preferenceSchemaVersion);
    object->setProperty ("revision", static_cast<juce::int64> (preference.revision));
    object->setProperty ("activation_seen", preference.activationSeen);
    object->setProperty ("first_activation_id",
                         preference.activationSeen ? juce::var (preference.firstActivationId)
                                                   : juce::var());
    object->setProperty ("choice", choiceName (preference.choice));
    object->setProperty ("notice_acknowledged", preference.noticeAcknowledged);
    return juce::JSON::toString (juce::var (object.release()), true) + "\n";
}

Preference defaultPreference()
{
    return {};
}

Snapshot makeSnapshot (const Preference& preference, bool persistent, std::uint64_t generation)
{
    return { generation, preference.revision, preference.activationSeen,
             preference.activationSeen && preference.choice != Choice::off,
             preference.activationSeen && ! preference.noticeAcknowledged,
             preference.choice, persistent };
}
}
