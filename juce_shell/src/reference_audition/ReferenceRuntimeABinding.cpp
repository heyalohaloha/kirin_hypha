#include "ReferenceRuntimeABinding.h"

#include <limits>
#include <regex>

namespace hypha::reference_audition
{
    namespace
    {
        constexpr std::int64_t maximumBindingBytes = 8 * 1024;
        constexpr std::int64_t maximumSafeInteger = 9'007'199'254'740'991;
        constexpr std::int64_t maximumLeaseMs = 10'000;

        bool matches (const juce::String& value, const char* expression)
        {
            return std::regex_match (value.toStdString(), std::regex (expression));
        }

        bool runtimeId (const juce::String& value)
        {
            return matches (value, R"(^[A-Za-z0-9._:-]{1,160}$)");
        }

        bool canonicalUuid (const juce::String& value)
        {
            return matches (value,
                R"(^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$)");
        }

        bool uuidV4 (const juce::String& value)
        {
            return matches (value,
                R"(^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$)");
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

        bool exactInteger (const juce::var& value, std::int64_t minimum,
                           std::int64_t maximum, std::int64_t& result)
        {
            if (! value.isInt() && ! value.isInt64())
                return false;
            result = static_cast<std::int64_t> (value);
            return result >= minimum && result <= maximum;
        }

        bool readBoundedJson (const juce::File& file, juce::var& value)
        {
            if (! file.existsAsFile() || file.isSymbolicLink())
                return false;
            const auto size = file.getSize();
            if (size < 1 || size > maximumBindingBytes
                || size > std::numeric_limits<int>::max())
                return false;
            auto stream = file.createInputStream();
            if (stream == nullptr || ! stream->openedOk())
                return false;
            juce::MemoryBlock bytes (static_cast<size_t> (size), true);
            if (stream->read (bytes.getData(), static_cast<int> (size)) != size)
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
    }

    RuntimeABindingRepository::RuntimeABindingRepository (juce::File transportRootIn)
        : root (std::move (transportRootIn))
    {
    }

    juce::File RuntimeABindingRepository::bindingFile (
        const juce::String& runtimeInstanceId) const
    {
        return runtimeId (runtimeInstanceId)
            ? root.getChildFile ("a_bindings").getChildFile (runtimeInstanceId + ".json")
            : juce::File {};
    }

    std::optional<RuntimeABinding> RuntimeABindingRepository::load (
        const RuntimeIdentity& identity, std::int64_t nowMs) const
    {
        if (! identity.valid() || ! runtimeId (identity.runtimeInstanceId)
            || ! canonicalUuid (identity.workId) || nowMs < 0
            || nowMs > maximumSafeInteger)
            return std::nullopt;
        juce::var value;
        if (! readBoundedJson (bindingFile (identity.runtimeInstanceId), value))
            return std::nullopt;
        const auto* object = value.getDynamicObject();
        if (object == nullptr || ! exactProperties (*object, {
                "format", "version", "binding_id", "runtime_instance_id",
                "host_process_id", "work_id", "recording_id", "issued_at_ms",
                "lease_expires_at_ms" })
            || object->getProperty ("format") != "kirin_hypha_reference_a_binding"
            || object->getProperty ("version") != "1.0")
            return std::nullopt;

        RuntimeABinding result;
        result.bindingId = object->getProperty ("binding_id").toString();
        result.runtimeInstanceId = object->getProperty ("runtime_instance_id").toString();
        result.workId = object->getProperty ("work_id").toString();
        result.recordingId = object->getProperty ("recording_id").toString();
        std::int64_t processId = 0;
        if (! uuidV4 (result.bindingId)
            || ! runtimeId (result.runtimeInstanceId)
            || result.runtimeInstanceId != identity.runtimeInstanceId
            || ! exactInteger (object->getProperty ("host_process_id"), 1,
                               std::numeric_limits<std::uint32_t>::max(), processId)
            || processId != identity.hostProcessId
            || ! canonicalUuid (result.workId) || result.workId != identity.workId
            || ! canonicalUuid (result.recordingId)
            || ! exactInteger (object->getProperty ("issued_at_ms"), 0,
                               maximumSafeInteger, result.issuedAtMs)
            || ! exactInteger (object->getProperty ("lease_expires_at_ms"), 1,
                               maximumSafeInteger, result.leaseExpiresAtMs)
            || result.issuedAtMs > nowMs || result.leaseExpiresAtMs < nowMs
            || result.leaseExpiresAtMs <= result.issuedAtMs
            || result.leaseExpiresAtMs - result.issuedAtMs > maximumLeaseMs)
            return std::nullopt;
        result.hostProcessId = static_cast<std::uint32_t> (processId);
        return result;
    }
}
