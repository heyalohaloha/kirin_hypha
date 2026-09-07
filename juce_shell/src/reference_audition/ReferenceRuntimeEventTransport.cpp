#include "ReferenceRuntimeEventTransport.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <regex>
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

        bool uuidV4Value (const juce::String& value)
        {
            static const std::regex pattern (
                R"(^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$)");
            return std::regex_match (value.toStdString(), pattern);
        }

        bool runtimeIdValue (const juce::String& value)
        {
            static const std::regex pattern (R"(^[A-Za-z0-9._:-]{1,160}$)");
            return std::regex_match (value.toStdString(), pattern);
        }

        bool sha256Value (const juce::String& value)
        {
            static const std::regex pattern (R"(^[a-f0-9]{64}$)");
            return std::regex_match (value.toStdString(), pattern);
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

        juce::var contentReceipt (const RuntimeSourcePresetReceipt& receipt)
        {
            auto object = new juce::DynamicObject();
            object->setProperty ("preset_id", receipt.presetId);
            object->setProperty ("revision_id", receipt.revisionId);
            object->setProperty ("relative_path", receipt.relativePath);
            object->setProperty ("sha256", receipt.sha256);
            object->setProperty ("bytes", receipt.bytes);
            return juce::var (object);
        }

        juce::var displaySnapshot (const RuntimeEventContext& context)
        {
            auto check = new juce::DynamicObject();
            check->setProperty ("check_id", context.checkId);
            check->setProperty ("label", context.checkLabel);
            juce::Array<juce::var> checks;
            checks.add (juce::var (check));

            auto candidate = new juce::DynamicObject();
            candidate->setProperty ("candidate_id", context.candidateId);
            candidate->setProperty ("display_name", context.candidateName);
            juce::Array<juce::var> candidates;
            candidates.add (juce::var (candidate));

            auto cue = new juce::DynamicObject();
            cue->setProperty ("cue_id", context.cueId);
            cue->setProperty ("label", context.cueLabel);
            juce::Array<juce::var> cues;
            cues.add (juce::var (cue));

            auto object = new juce::DynamicObject();
            object->setProperty ("preset_name", context.presetName);
            object->setProperty ("checks", juce::var (checks));
            object->setProperty ("candidates", juce::var (candidates));
            object->setProperty ("cues", juce::var (cues));
            return juce::var (object);
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
                if (! std::isfinite (number)) return {};
                return juce::JSON::toString (value, true);
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
            if (bytes < 1 || bytes > maximumRuntimeEventBytes
                || ! target.getParentDirectory().createDirectory())
                return false;
            if (target.existsAsFile())
                return target.loadFileAsString() == content;
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

    bool RuntimeEventContext::valid() const noexcept
    {
        return identity.valid() && manifestRevision > 0
            && uuidV4Value (presetArtifact.presetId)
            && uuidV4Value (presetArtifact.revisionId)
            && presetArtifact.relativePath.isNotEmpty()
            && sha256Value (presetArtifact.sha256) && presetArtifact.bytes > 0
            && uuidV4Value (checkId) && checkLabel.isNotEmpty()
            && uuidV4Value (candidateId) && candidateName.isNotEmpty()
            && uuidV4Value (cueId) && cueLabel.isNotEmpty()
            && (comparisonMode == "original" || comparisonMode == "loudness_match"
                || comparisonMode == "peak_match");
    }

    RuntimeEventTransport::RuntimeEventTransport (juce::File transportRootIn)
        : root (std::move (transportRootIn))
    {
    }

    juce::String RuntimeEventTransport::uuidV4()
    {
        return juce::Uuid().toDashedString().toLowerCase();
    }

    juce::String RuntimeEventTransport::canonicalJson (const juce::var& value)
    {
        return canonicalValue (value);
    }

    juce::File RuntimeEventTransport::eventFile (
        const juce::String& runtimeInstanceId, const juce::String& eventId) const
    {
        return runtimeIdValue (runtimeInstanceId) && uuidV4Value (eventId)
            ? root.getChildFile ("events").getChildFile (runtimeInstanceId)
                  .getChildFile (eventId + ".json")
            : juce::File {};
    }

    RuntimeEventWriteResult RuntimeEventTransport::write (
        const RuntimeEventContext& context, const juce::String& eventId,
        const juce::String& runId, const juce::String& eventType,
        std::int64_t occurredAtMs, const juce::var& note, const juce::var& payload) const
    {
        RuntimeEventWriteResult result;
        result.eventId = eventId;
        if (! context.valid() || ! uuidV4Value (eventId) || ! uuidV4Value (runId)
            || occurredAtMs < 0 || occurredAtMs > maxSafeJsonInteger)
            return result;
        auto object = new juce::DynamicObject();
        object->setProperty ("format", "kirin_hypha_reference_event");
        object->setProperty ("version", "1.0");
        object->setProperty ("event_id", eventId);
        object->setProperty ("runtime_instance_id", context.identity.runtimeInstanceId);
        object->setProperty ("host_process_id",
                             static_cast<juce::int64> (context.identity.hostProcessId));
        object->setProperty ("work_id", context.identity.workId);
        object->setProperty ("manifest_revision", context.manifestRevision);
        object->setProperty ("event_type", eventType);
        object->setProperty ("occurred_at_ms", occurredAtMs);
        object->setProperty ("run_id", runId);
        object->setProperty ("preset_artifact", contentReceipt (context.presetArtifact));
        object->setProperty ("display_snapshot", displaySnapshot (context));
        object->setProperty ("note", note);
        object->setProperty ("payload", payload);
        result.canonicalJson = canonicalJson (juce::var (object));
        const auto file = eventFile (context.identity.runtimeInstanceId, eventId);
        result.written = file != juce::File() && result.canonicalJson.isNotEmpty()
                      && writeImmutable (file, result.canonicalJson);
        return result;
    }

    RuntimeEventWriteResult RuntimeEventTransport::writeAuditionStarted (
        const RuntimeEventContext& context, const juce::String& eventId,
        const juce::String& runId, std::int64_t occurredAtMs) const
    {
        auto payload = new juce::DynamicObject();
        payload->setProperty ("check_id", context.checkId);
        payload->setProperty ("candidate_id", context.candidateId);
        payload->setProperty ("cue_id", context.cueId);
        payload->setProperty ("comparison_mode", context.comparisonMode);
        return write (context, eventId, runId, "audition_started", occurredAtMs,
                      juce::var(), juce::var (payload));
    }

    RuntimeEventWriteResult RuntimeEventTransport::writeAuditionCompleted (
        const RuntimeEventContext& context, const juce::String& eventId,
        const juce::String& runId, const juce::String& startedEventId,
        std::int64_t occurredAtMs, std::int64_t bConfirmedSwitches,
        std::int64_t aConfirmedSwitches) const
    {
        if (! uuidV4Value (startedEventId) || bConfirmedSwitches < 1
            || aConfirmedSwitches < 0)
            return {};
        auto selected = new juce::DynamicObject();
        selected->setProperty ("candidate_id", context.candidateId);
        selected->setProperty ("cue_id", context.cueId);
        selected->setProperty ("confirmed_switches", bConfirmedSwitches);
        juce::Array<juce::var> switches;
        switches.add (juce::var (selected));
        auto payload = new juce::DynamicObject();
        payload->setProperty ("started_event_id", startedEventId);
        payload->setProperty ("a_confirmed_switches", aConfirmedSwitches);
        payload->setProperty ("candidate_switches", juce::var (switches));
        return write (context, eventId, runId, "audition_completed", occurredAtMs,
                      juce::var(), juce::var (payload));
    }

    RuntimeEventWriteResult RuntimeEventTransport::writeBlindStarted (
        const RuntimeEventContext& context, const juce::String& eventId,
        const juce::String& runId, std::int64_t occurredAtMs,
        const juce::var& trialStart) const
    {
        auto payload = new juce::DynamicObject();
        payload->setProperty ("trial_start", trialStart);
        return write (context, eventId, runId, "blind_compare_started", occurredAtMs,
                      juce::var(), juce::var (payload));
    }

    RuntimeEventWriteResult RuntimeEventTransport::writeBlindCompleted (
        const RuntimeEventContext& context, const juce::String& eventId,
        const juce::String& runId, std::int64_t occurredAtMs,
        const juce::var& trialCompleted) const
    {
        auto payload = new juce::DynamicObject();
        payload->setProperty ("trial_completed", trialCompleted);
        return write (context, eventId, runId, "blind_compare_completed", occurredAtMs,
                      juce::var(), juce::var (payload));
    }
}
