#include "ReferenceRecoveryTransport.h"

#include <cmath>
#include <limits>
#include <regex>
#include <set>
#include <utility>

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

        bool safeRuntimeId (const juce::String& value)
        {
            static const std::regex pattern (R"(^[A-Za-z0-9._:-]{1,160}$)");
            return std::regex_match (value.toStdString(), pattern);
        }

        bool canonicalUuidV4 (const juce::String& value)
        {
            static const std::regex pattern (
                R"(^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$)");
            return std::regex_match (value.toStdString(), pattern);
        }

        bool workUuid (const juce::String& value)
        {
            static const std::regex pattern (
                R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$)");
            return std::regex_match (value.toStdString(), pattern);
        }

        bool validTime (std::int64_t value)
        {
            return value >= 0 && value <= maxSafeJsonInteger;
        }

        bool exactProperties (const juce::DynamicObject& object,
                              std::initializer_list<const char*> names)
        {
            std::set<std::string> expected;
            for (const auto* name : names)
                expected.emplace (name);
            const auto& properties = object.getProperties();
            if (properties.size() != static_cast<int> (expected.size()))
                return false;
            for (int index = 0; index < properties.size(); ++index)
                if (expected.find (properties.getName (index).toString().toStdString())
                    == expected.end())
                    return false;
            return true;
        }

        bool validContext (RecoveryDestination destination, const RecoveryAuthority& authority,
                           const RecoveryContext& context)
        {
            const bool preset = canonicalUuidV4 (context.presetId);
            const bool check = canonicalUuidV4 (context.checkId);
            const bool candidate = canonicalUuidV4 (context.candidateId);
            const bool empty = context.presetId.isEmpty() && context.checkId.isEmpty()
                            && context.candidateId.isEmpty();
            if (destination == RecoveryDestination::workBinding)
                return authority.workId.isEmpty() && empty;
            if (! workUuid (authority.workId))
                return false;
            if (destination == RecoveryDestination::candidateSource
                || destination == RecoveryDestination::candidateMeasurement)
                return preset && check && candidate;
            if ((! context.candidateId.isEmpty() && (! preset || ! check || ! candidate))
                || (! context.checkId.isEmpty() && (! preset || ! check))
                || (! context.presetId.isEmpty() && ! preset))
                return false;
            return true;
        }

        juce::String destinationText (RecoveryDestination destination)
        {
            switch (destination)
            {
                case RecoveryDestination::reference:            return "reference";
                case RecoveryDestination::workBinding:          return "work_binding";
                case RecoveryDestination::candidateSource:      return "candidate_source";
                case RecoveryDestination::candidateMeasurement: return "candidate_measurement";
                case RecoveryDestination::diagnostics:          return "diagnostics";
            }
            return {};
        }

        std::optional<RecoveryOutcome> parseOutcome (const juce::String& value)
        {
            if (value == "exact_opened") return RecoveryOutcome::exactOpened;
            if (value == "safe_fallback_opened") return RecoveryOutcome::safeFallbackOpened;
            if (value == "rejected") return RecoveryOutcome::rejected;
            return std::nullopt;
        }

        juce::var nullableId (const juce::String& value)
        {
            return value.isEmpty() ? juce::var() : juce::var (value);
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

        bool replaceJsonAtomically (const juce::File& target, const juce::var& value,
                                    std::int64_t maximumBytes)
        {
            const auto content = juce::JSON::toString (value, true) + "\n";
            const auto bytes = static_cast<std::int64_t> (content.getNumBytesAsUTF8());
            if (bytes < 1 || bytes > maximumBytes
                || ! target.getParentDirectory().createDirectory())
                return false;
            juce::TemporaryFile temporary (target);
            {
                auto stream = temporary.getFile().createOutputStream();
                if (stream == nullptr || ! stream->openedOk()
                    || ! stream->write (content.toRawUTF8(), static_cast<size_t> (bytes)))
                    return false;
                stream->flush();
                if (stream->getStatus().failed())
                    return false;
            }
            if (! makePrivate (temporary.getFile())
                || ! temporary.overwriteTargetFileWithTemporary())
                return false;
            return fsyncDirectory (target.getParentDirectory());
        }

        bool readBoundedJson (const juce::File& file, std::int64_t maximumBytes,
                              juce::var& value)
        {
            if (! file.existsAsFile() || file.isSymbolicLink())
                return false;
            const auto bytes = file.getSize();
            if (bytes < 1 || bytes > maximumBytes || bytes > std::numeric_limits<int>::max())
                return false;
            juce::MemoryBlock content;
            auto stream = file.createInputStream();
            if (stream == nullptr || ! stream->openedOk())
                return false;
            content.setSize (static_cast<size_t> (bytes), false);
            if (stream->read (content.getData(), static_cast<int> (bytes)) != bytes)
                return false;
            const auto* raw = static_cast<const char*> (content.getData());
            if (bytes >= 3 && static_cast<unsigned char> (raw[0]) == 0xef
                && static_cast<unsigned char> (raw[1]) == 0xbb
                && static_cast<unsigned char> (raw[2]) == 0xbf)
                return false;
            if (! juce::CharPointer_UTF8::isValidString (raw, static_cast<int> (bytes)))
                return false;
            value = juce::JSON::parse (juce::String::fromUTF8 (raw, static_cast<int> (bytes)));
            return ! value.isVoid();
        }

        bool integerValue (const juce::var& value, std::int64_t minimum,
                           std::int64_t maximum, std::int64_t& result)
        {
            if (! value.isInt() && ! value.isInt64())
                return false;
            result = static_cast<std::int64_t> (value);
            return result >= minimum && result <= maximum;
        }
    }

    RecoveryTransport::RecoveryTransport (juce::File transportRootIn)
        : root (std::move (transportRootIn))
    {
    }

    juce::File RecoveryTransport::transportRoot()
    {
       #if JUCE_WINDOWS
        auto local = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
        if (local == juce::File())
        {
            const auto profile = juce::SystemStats::getEnvironmentVariable ("USERPROFILE", {});
            if (profile.isNotEmpty())
                local = juce::File (profile).getChildFile ("AppData").getChildFile ("Local");
        }
        return local.getChildFile ("Kirin OS").getChildFile ("plugin_data")
                    .getChildFile ("reference").getChildFile ("v2");
       #else
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        return home.getChildFile ("Library").getChildFile ("Application Support")
                   .getChildFile ("Kirin OS").getChildFile ("plugin_data")
                   .getChildFile ("reference").getChildFile ("v2");
       #endif
    }

    juce::File RecoveryTransport::requestFile (const juce::String& runtimeInstanceId) const
    {
        return safeRuntimeId (runtimeInstanceId)
            ? root.getChildFile ("recovery_requests").getChildFile (runtimeInstanceId + ".json")
            : juce::File {};
    }

    juce::File RecoveryTransport::acknowledgementFile (
        const juce::String& runtimeInstanceId) const
    {
        return safeRuntimeId (runtimeInstanceId)
            ? root.getChildFile ("recovery_acknowledgements")
                  .getChildFile (runtimeInstanceId + ".json")
            : juce::File {};
    }

    std::optional<RecoveryRequest> RecoveryTransport::writeRequest (
        const RecoveryAuthority& authority,
        RecoveryDestination destination,
        const RecoveryContext& context,
        std::int64_t requestedAtMs,
        juce::String requestId) const
    {
        if (requestId.isEmpty())
            requestId = juce::Uuid().toDashedString();
        if (! safeRuntimeId (authority.runtimeInstanceId) || authority.hostProcessId == 0
            || ! canonicalUuidV4 (requestId) || ! validTime (requestedAtMs)
            || ! validContext (destination, authority, context))
            return std::nullopt;
        const auto file = requestFile (authority.runtimeInstanceId);
        if (file == juce::File())
            return std::nullopt;

        auto contextObject = new juce::DynamicObject();
        contextObject->setProperty ("preset_id", nullableId (context.presetId));
        contextObject->setProperty ("check_id", nullableId (context.checkId));
        contextObject->setProperty ("candidate_id", nullableId (context.candidateId));
        auto object = new juce::DynamicObject();
        object->setProperty ("format", "kirin_hypha_reference_recovery_request");
        object->setProperty ("version", "1.0");
        object->setProperty ("request_id", requestId);
        object->setProperty ("runtime_instance_id", authority.runtimeInstanceId);
        object->setProperty ("host_process_id",
                             static_cast<juce::int64> (authority.hostProcessId));
        object->setProperty ("work_id", nullableId (authority.workId));
        object->setProperty ("destination", destinationText (destination));
        object->setProperty ("context", juce::var (contextObject));
        object->setProperty ("requested_at_ms", requestedAtMs);
        if (! replaceJsonAtomically (file, juce::var (object), maximumRecoveryRequestBytes))
            return std::nullopt;
        return RecoveryRequest { requestId, authority, destination, context, requestedAtMs };
    }

    std::optional<RecoveryAcknowledgement> RecoveryTransport::loadAcknowledgement (
        const RecoveryRequest& request) const
    {
        if (! canonicalUuidV4 (request.requestId)
            || ! safeRuntimeId (request.authority.runtimeInstanceId)
            || request.authority.hostProcessId == 0 || ! validTime (request.requestedAtMs)
            || ! validContext (request.destination, request.authority, request.context))
            return std::nullopt;
        juce::var value;
        if (! readBoundedJson (acknowledgementFile (request.authority.runtimeInstanceId),
                               maximumRecoveryAcknowledgementBytes, value))
            return std::nullopt;
        const auto* object = value.getDynamicObject();
        if (object == nullptr || ! exactProperties (*object, {
                "format", "version", "request_id", "runtime_instance_id",
                "host_process_id", "outcome", "handled_at_ms" })
            || object->getProperty ("format").toString()
                != "kirin_hypha_reference_recovery_acknowledgement"
            || object->getProperty ("version").toString() != "1.0")
            return std::nullopt;

        RecoveryAcknowledgement result;
        result.requestId = object->getProperty ("request_id").toString();
        result.runtimeInstanceId = object->getProperty ("runtime_instance_id").toString();
        std::int64_t processId = 0;
        if (! canonicalUuidV4 (result.requestId)
            || result.requestId != request.requestId
            || ! safeRuntimeId (result.runtimeInstanceId)
            || result.runtimeInstanceId != request.authority.runtimeInstanceId
            || ! integerValue (object->getProperty ("host_process_id"), 1,
                               std::numeric_limits<std::uint32_t>::max(), processId)
            || processId != request.authority.hostProcessId)
            return std::nullopt;
        result.hostProcessId = static_cast<std::uint32_t> (processId);
        const auto outcome = parseOutcome (object->getProperty ("outcome").toString());
        if (! outcome.has_value()
            || ! integerValue (object->getProperty ("handled_at_ms"), request.requestedAtMs,
                               maxSafeJsonInteger, result.handledAtMs)
            || (*outcome == RecoveryOutcome::safeFallbackOpened
                && request.destination != RecoveryDestination::candidateSource
                && request.destination != RecoveryDestination::candidateMeasurement))
            return std::nullopt;
        result.outcome = *outcome;
        return result;
    }
}
