#include "ReferenceRuntimeV2PresetParsing.h"

#include <regex>
#include <set>
#include <utility>

namespace hypha::reference_audition::runtime_v2_parsing
{
namespace
{
constexpr std::int64_t maximumPresetBytes = 2 * 1024 * 1024;
constexpr std::int64_t maximumSourcePresetBytes = 8 * 1024 * 1024;

bool matches (const juce::String& value, const char* expression)
{
    return std::regex_match (value.toStdString(), std::regex (expression));
}

bool uuidV4 (const juce::String& value)
{
    return matches (value,
                    R"(^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$)");
}

bool sha256 (const juce::String& value)
{
    return matches (value, R"(^[a-f0-9]{64}$)");
}

bool exactProperties (const juce::DynamicObject& object,
                      std::initializer_list<const char*> names)
{
    if (object.getProperties().size() != static_cast<int> (names.size()))
        return false;
    for (const auto* name : names)
        if (! object.hasProperty (name))
            return false;
    return true;
}

bool exactInteger (const juce::var& value, std::int64_t maximum,
                   std::int64_t& result)
{
    if (! value.isInt() && ! value.isInt64())
        return false;
    result = static_cast<std::int64_t> (value);
    return result >= 1 && result <= maximum;
}

bool exactString (const juce::var& value, juce::String& result)
{
    if (! value.isString())
        return false;
    result = value.toString();
    return true;
}

bool displayText (const juce::var& value, int maximumCharacters,
                  juce::String& result)
{
    if (! exactString (value, result) || result.isEmpty()
        || result.length() > maximumCharacters || result.trim() != result)
        return false;
    for (auto character : result)
        if (character < 0x20 || (character >= 0x7f && character <= 0x9f)
            || character == 0x2028 || character == 0x2029)
            return false;
    return true;
}
}

bool parseSourcePresetReceipt (const juce::var& value,
                               RuntimeSourcePresetReceipt& result)
{
    const auto* object = value.getDynamicObject();
    return object != nullptr && exactProperties (*object, {
               "preset_id", "revision_id", "relative_path", "sha256", "bytes" })
        && exactString (object->getProperty ("preset_id"), result.presetId)
        && exactString (object->getProperty ("revision_id"), result.revisionId)
        && exactString (object->getProperty ("relative_path"), result.relativePath)
        && exactString (object->getProperty ("sha256"), result.sha256)
        && uuidV4 (result.presetId) && uuidV4 (result.revisionId)
        && sha256 (result.sha256)
        && result.relativePath == "reference/presets/" + result.presetId + "/"
                                   + result.revisionId + ".v1.json"
        && exactInteger (object->getProperty ("bytes"),
                         maximumSourcePresetBytes, result.bytes);
}

bool parseGlobalPresetCatalog (const juce::var& value,
                               RuntimeGlobalPresetCatalog& result)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr || ! exactProperties (*object, { "format", "version", "presets" })
        || object->getProperty ("format") != "kirin_hypha_reference_global_preset_catalog"
        || object->getProperty ("version") != "1.0")
        return false;
    const auto* presets = object->getProperty ("presets").getArray();
    if (presets == nullptr || presets->size() < 5 || presets->size() > 133)
        return false;
    std::set<std::string> presetIds, revisionIds;
    for (int index = 0; index < presets->size(); ++index)
    {
        const auto* entry = presets->getReference (index).getDynamicObject();
        RuntimeGlobalPresetCatalogEntry parsed;
        if (entry == nullptr || ! exactProperties (*entry, {
                "preset_id", "revision_id", "name_snapshot", "origin" })
            || ! exactString (entry->getProperty ("preset_id"), parsed.presetId)
            || ! exactString (entry->getProperty ("revision_id"), parsed.revisionId)
            || ! displayText (entry->getProperty ("name_snapshot"), 80,
                              parsed.nameSnapshot)
            || ! exactString (entry->getProperty ("origin"), parsed.origin)
            || ! uuidV4 (parsed.presetId) || ! uuidV4 (parsed.revisionId)
            || parsed.origin != (index < 5 ? "factory" : "user")
            || ! presetIds.emplace (parsed.presetId.toStdString()).second
            || ! revisionIds.emplace (parsed.revisionId.toStdString()).second)
            return false;
        result.presets.push_back (std::move (parsed));
    }
    return true;
}

bool parsePresetReceipt (const juce::var& value, const juce::String& workId,
                         RuntimePresetReceipt& result)
{
    const auto* object = value.getDynamicObject();
    return object != nullptr && exactProperties (*object, {
               "preset_id", "revision_id", "relative_path", "sha256", "bytes" })
        && exactString (object->getProperty ("preset_id"), result.presetId)
        && exactString (object->getProperty ("revision_id"), result.revisionId)
        && exactString (object->getProperty ("relative_path"), result.relativePath)
        && exactString (object->getProperty ("sha256"), result.sha256)
        && uuidV4 (result.presetId) && uuidV4 (result.revisionId)
        && sha256 (result.sha256)
        && result.relativePath == "plugin_data/reference/v2/presets/" + workId
                                  + "/" + result.presetId + ".json"
        && exactInteger (object->getProperty ("bytes"), maximumPresetBytes, result.bytes);
}
}
