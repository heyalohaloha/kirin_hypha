#include "ReferencePresetSelectionTransport.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <utility>
#include <vector>

#if ! JUCE_WINDOWS
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace hypha::reference_audition
{
    namespace
    {
        constexpr std::int64_t maxSafeJsonInteger = 9'007'199'254'740'991;

        bool runtimeIdValue (const juce::String& value)
        {
            static const std::regex pattern (R"(^[A-Za-z0-9._:-]{1,160}$)");
            return std::regex_match (value.toStdString(), pattern);
        }

        bool uuidValue (const juce::String& value)
        {
            static const std::regex pattern (
                R"(^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$)");
            return std::regex_match (value.toStdString(), pattern);
        }

        bool uuidV4Value (const juce::String& value)
        {
            static const std::regex pattern (
                R"(^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$)");
            return std::regex_match (value.toStdString(), pattern);
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

        bool exactString (const juce::var& value, juce::String& result)
        {
            if (! value.isString()) return false;
            result = value.toString();
            return true;
        }

        bool exactInteger (const juce::var& value, std::int64_t minimum,
                           std::int64_t maximum, std::int64_t& result)
        {
            if (! value.isInt() && ! value.isInt64()) return false;
            result = static_cast<std::int64_t> (value);
            return result >= minimum && result <= maximum;
        }

        bool readJson (const juce::File& file, std::int64_t maximumBytes,
                       juce::var& value)
        {
            if (! file.existsAsFile() || file.isSymbolicLink()) return false;
            const auto size = file.getSize();
            if (size < 1 || size > maximumBytes || size > std::numeric_limits<int>::max())
                return false;
            juce::MemoryBlock bytes (static_cast<size_t> (size), true);
            auto stream = file.createInputStream();
            if (stream == nullptr || ! stream->openedOk()
                || stream->read (bytes.getData(), static_cast<int> (size)) != size)
                return false;
            const auto* raw = static_cast<const char*> (bytes.getData());
            if (size >= 3 && static_cast<unsigned char> (raw[0]) == 0xef
                && static_cast<unsigned char> (raw[1]) == 0xbb
                && static_cast<unsigned char> (raw[2]) == 0xbf)
                return false;
            if (! juce::CharPointer_UTF8::isValidString (raw, static_cast<int> (size)))
                return false;
            value = juce::JSON::parse (juce::String::fromUTF8 (raw, static_cast<int> (size)));
            return ! value.isVoid();
        }

        bool recoveryReason (const juce::String& value)
        {
            return value == "preset_setup_required" || value == "source_unavailable"
                || value == "measurement_required" || value == "request_stale"
                || value == "work_unavailable" || value == "storage_unavailable"
                || value == "publication_failed" || value == "request_invalid";
        }

        bool recoveryAction (const juce::String& value)
        {
            return value == "retry" || value == "open_reference"
                || value == "choose_source" || value == "measure_source";
        }

        bool fsyncDirectory (const juce::File& directory)
        {
           #if JUCE_WINDOWS
            juce::ignoreUnused (directory);
            return true;
           #else
            const int handle = ::open (directory.getFullPathName().toRawUTF8(), O_RDONLY);
            if (handle < 0)
                return false;
            const bool ok = ::fsync (handle) == 0;
            ::close (handle);
            return ok;
           #endif
        }

        bool makePrivate (const juce::File& file)
        {
           #if JUCE_WINDOWS
            juce::ignoreUnused (file);
            return true;
           #else
            return ::chmod (file.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR) == 0;
           #endif
        }

        juce::String canonicalValue (const juce::var& value)
        {
            if (value.isVoid() || value.isUndefined()) return "null";
            if (value.isBool()) return static_cast<bool> (value) ? "true" : "false";
            if (value.isInt()) return juce::String (static_cast<int> (value));
            if (value.isInt64()) return juce::String (static_cast<juce::int64> (value));
            if (value.isDouble())
            {
                const auto number = static_cast<double> (value);
                return std::isfinite (number) ? juce::JSON::toString (value, true)
                                             : juce::String {};
            }
            if (value.isString()) return juce::JSON::toString (value, true);
            if (const auto* array = value.getArray())
            {
                juce::String output ("[");
                for (int index = 0; index < array->size(); ++index)
                {
                    if (index != 0) output += ",";
                    const auto item = canonicalValue (array->getReference (index));
                    if (item.isEmpty()) return {};
                    output += item;
                }
                return output + "]";
            }
            const auto* object = value.getDynamicObject();
            if (object == nullptr) return {};
            std::vector<juce::Identifier> keys;
            const auto& properties = object->getProperties();
            keys.reserve (static_cast<size_t> (properties.size()));
            for (int index = 0; index < properties.size(); ++index)
                keys.push_back (properties.getName (index));
            std::sort (keys.begin(), keys.end(), [] (const auto& left, const auto& right) {
                return left.toString().compare (right.toString()) < 0;
            });
            juce::String output ("{");
            for (size_t index = 0; index < keys.size(); ++index)
            {
                if (index != 0) output += ",";
                const auto child = canonicalValue (object->getProperty (keys[index]));
                if (child.isEmpty()) return {};
                output += juce::JSON::toString (juce::var (keys[index].toString()), true)
                       + ":" + child;
            }
            return output + "}";
        }

        bool writeImmutable (const juce::File& target, const juce::String& content)
        {
            const auto bytes = static_cast<std::int64_t> (content.getNumBytesAsUTF8());
            if (bytes < 1 || bytes > maximumPresetSelectionRequestBytes
                || ! target.getParentDirectory().createDirectory())
                return false;
            if (target.existsAsFile())
                return target.loadFileAsString() == content;
            if (target.exists())
                return false;
            const auto temporary = target.getSiblingFile (
                "." + target.getFileName() + "." + juce::Uuid().toDashedString() + ".tmp");
            auto stream = temporary.createOutputStream();
            if (stream == nullptr || ! stream->openedOk()
                || ! stream->write (content.toRawUTF8(), static_cast<size_t> (bytes)))
                return false;
            stream->flush();
            const bool flushed = ! stream->getStatus().failed();
            stream.reset();
            if (! flushed || ! makePrivate (temporary)
                || target.exists() || ! temporary.moveFileTo (target))
            {
                temporary.deleteFile();
                return false;
            }
            return fsyncDirectory (target.getParentDirectory());
        }
    }

    PresetSelectionTransport::PresetSelectionTransport (juce::File transportRootIn)
        : root (std::move (transportRootIn))
    {
    }

    juce::File PresetSelectionTransport::requestFile (
        const juce::String& runtimeInstanceId, const juce::String& requestId) const
    {
        return runtimeIdValue (runtimeInstanceId) && uuidV4Value (requestId)
            ? root.getChildFile ("preset_selection_requests")
                  .getChildFile (runtimeInstanceId).getChildFile (requestId + ".json")
            : juce::File {};
    }

    juce::File PresetSelectionTransport::acknowledgementFile (
        const juce::String& runtimeInstanceId, const juce::String& requestId) const
    {
        return runtimeIdValue (runtimeInstanceId) && uuidV4Value (requestId)
            ? root.getChildFile ("preset_selection_acknowledgements")
                  .getChildFile (runtimeInstanceId).getChildFile (requestId + ".json")
            : juce::File {};
    }

    std::optional<PresetSelectionRequest> PresetSelectionTransport::writeRequest (
        const RuntimeIdentity& identity,
        std::int64_t manifestRevision,
        const RuntimeGlobalPresetCatalogEntry& selectedPreset,
        std::int64_t requestedAtMs,
        juce::String requestId) const
    {
        if (requestId.isEmpty())
            requestId = juce::Uuid().toDashedString().toLowerCase();
        if (! runtimeIdValue (identity.runtimeInstanceId)
            || ! uuidValue (identity.workId) || identity.hostProcessId == 0
            || ! uuidV4Value (requestId)
            || manifestRevision < 1 || manifestRevision > maxSafeJsonInteger
            || requestedAtMs < 0 || requestedAtMs > maxSafeJsonInteger
            || ! uuidV4Value (selectedPreset.presetId)
            || ! uuidV4Value (selectedPreset.revisionId))
            return std::nullopt;

        auto selected = new juce::DynamicObject();
        selected->setProperty ("preset_id", selectedPreset.presetId);
        selected->setProperty ("revision_id", selectedPreset.revisionId);
        auto object = new juce::DynamicObject();
        object->setProperty ("format", "kirin_hypha_reference_preset_selection_request");
        object->setProperty ("version", "1.0");
        object->setProperty ("request_id", requestId);
        object->setProperty ("runtime_instance_id", identity.runtimeInstanceId);
        object->setProperty ("host_process_id",
                             static_cast<juce::int64> (identity.hostProcessId));
        object->setProperty ("work_id", identity.workId);
        object->setProperty ("manifest_revision", manifestRevision);
        object->setProperty ("requested_at_ms", requestedAtMs);
        object->setProperty ("selected_preset", juce::var (selected));
        const auto canonicalJson = canonicalValue (juce::var (object));
        const auto file = requestFile (identity.runtimeInstanceId, requestId);
        if (file == juce::File() || canonicalJson.isEmpty()
            || ! writeImmutable (file, canonicalJson))
            return std::nullopt;
        return PresetSelectionRequest {
            requestId,
            identity,
            manifestRevision,
            requestedAtMs,
            selectedPreset,
            canonicalJson,
        };
    }

    std::optional<PresetSelectionAcknowledgement>
    PresetSelectionTransport::loadAcknowledgement (
        const PresetSelectionRequest& request) const
    {
        juce::var json;
        const auto file = acknowledgementFile (
            request.identity.runtimeInstanceId, request.requestId);
        if (file == juce::File()
            || ! readJson (file, maximumPresetSelectionAcknowledgementBytes, json))
            return std::nullopt;
        const auto* object = json.getDynamicObject();
        if (object == nullptr || ! exactProperties (*object, {
                "format", "version", "request_id", "runtime_instance_id",
                "host_process_id", "work_id", "handled_at_ms", "outcome",
                "prepared_preset", "recovery" })
            || object->getProperty ("format")
                   != "kirin_hypha_reference_preset_selection_acknowledgement"
            || object->getProperty ("version") != "1.0")
            return std::nullopt;
        PresetSelectionAcknowledgement result;
        std::int64_t processId = 0;
        juce::String outcome;
        if (! exactString (object->getProperty ("request_id"), result.requestId)
            || ! exactString (object->getProperty ("runtime_instance_id"),
                              result.identity.runtimeInstanceId)
            || ! exactInteger (object->getProperty ("host_process_id"), 1,
                               4'294'967'295, processId)
            || ! exactString (object->getProperty ("work_id"), result.identity.workId)
            || ! exactInteger (object->getProperty ("handled_at_ms"), 0,
                               maxSafeJsonInteger, result.handledAtMs)
            || ! exactString (object->getProperty ("outcome"), outcome)
            || result.requestId != request.requestId
            || result.identity.runtimeInstanceId != request.identity.runtimeInstanceId
            || processId != static_cast<std::int64_t> (request.identity.hostProcessId)
            || result.identity.workId != request.identity.workId
            || result.handledAtMs < request.requestedAtMs)
            return std::nullopt;
        result.identity.hostProcessId = static_cast<std::uint32_t> (processId);

        const auto preparedValue = object->getProperty ("prepared_preset");
        const auto recoveryValue = object->getProperty ("recovery");
        if (outcome == "prepared")
        {
            const auto* prepared = preparedValue.getDynamicObject();
            RuntimeGlobalPresetCatalogEntry identity;
            if (prepared == nullptr || ! recoveryValue.isVoid()
                || ! exactProperties (*prepared, { "preset_id", "revision_id" })
                || ! exactString (prepared->getProperty ("preset_id"), identity.presetId)
                || ! exactString (prepared->getProperty ("revision_id"), identity.revisionId)
                || ! uuidV4Value (identity.presetId) || ! uuidV4Value (identity.revisionId))
                return std::nullopt;
            result.outcome = PresetSelectionOutcome::prepared;
            result.preparedPreset = std::move (identity);
        }
        else
        {
            if (! preparedValue.isVoid()) return std::nullopt;
            const auto* recovery = recoveryValue.getDynamicObject();
            PresetSelectionRecovery parsed;
            if (recovery == nullptr
                || ! exactProperties (*recovery, { "reason", "action" })
                || ! exactString (recovery->getProperty ("reason"), parsed.reason)
                || ! exactString (recovery->getProperty ("action"), parsed.action)
                || ! recoveryReason (parsed.reason) || ! recoveryAction (parsed.action))
                return std::nullopt;
            if (outcome == "action_required")
                result.outcome = PresetSelectionOutcome::actionRequired;
            else if (outcome == "rejected")
                result.outcome = PresetSelectionOutcome::rejected;
            else
                return std::nullopt;
            result.recovery = std::move (parsed);
        }
        return result;
    }

    bool PresetSelectionTransport::removeExchange (
        const PresetSelectionRequest& request) const
    {
        bool ok = true;
        const auto requestPath = requestFile (
            request.identity.runtimeInstanceId, request.requestId);
        const auto acknowledgementPath = acknowledgementFile (
            request.identity.runtimeInstanceId, request.requestId);
        if (requestPath.exists() && ! requestPath.deleteFile()) ok = false;
        if (acknowledgementPath.exists() && ! acknowledgementPath.deleteFile()) ok = false;
        return ok;
    }
}
