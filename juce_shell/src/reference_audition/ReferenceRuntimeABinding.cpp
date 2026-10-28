#include "ReferenceRuntimeABinding.h"
#include "ReferenceTestCommitObserver.h"

#include <limits>
#include <regex>
#include <stdexcept>
#if JUCE_WINDOWS && defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

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
           #if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
            const bool exists = file.existsAsFile();
           #if JUCE_WINDOWS
            const auto error = exists ? 0ul : GetLastError();
           #else
            const auto error = 0ul;
           #endif
            const bool symbolic = exists && file.isSymbolicLink();
            KIRIN_REFERENCE_BINDING_ERROR_FOR_TEST (error);
            KIRIN_REFERENCE_BINDING_STAGE_FOR_TEST (!exists ? "file_not_regular" : symbolic ? "file_symlink" : "file_facts");
            if (!exists || symbolic) return false;
           #else
            if (! file.existsAsFile() || file.isSymbolicLink()) return false;
           #endif
            KIRIN_REFERENCE_BINDING_STAGE_FOR_TEST ("file_size");
            const auto size = file.getSize();
            if (size < 1 || size > maximumBindingBytes
                || size > std::numeric_limits<int>::max())
                return false;
            KIRIN_REFERENCE_BINDING_STAGE_FOR_TEST ("file_open");
            auto stream = file.createInputStream();
            if (stream == nullptr || ! stream->openedOk())
                return false;
            juce::MemoryBlock bytes (static_cast<size_t> (size), true);
            KIRIN_REFERENCE_BINDING_STAGE_FOR_TEST ("file_read");
            if (stream->read (bytes.getData(), static_cast<int> (size)) != size)
                return false;
            KIRIN_REFERENCE_BINDING_STAGE_FOR_TEST ("encoding");
            const auto* raw = static_cast<const char*> (bytes.getData());
            if (size >= 3 && static_cast<unsigned char> (raw[0]) == 0xef
                && static_cast<unsigned char> (raw[1]) == 0xbb
                && static_cast<unsigned char> (raw[2]) == 0xbf)
                return false;
            if (! juce::CharPointer_UTF8::isValidString (raw, static_cast<int> (size)))
                return false;
            KIRIN_REFERENCE_BINDING_STAGE_FOR_TEST ("json_parse");
            value = juce::JSON::parse (juce::String::fromUTF8 (raw, static_cast<int> (size)));
            return ! value.isVoid();
        }
    }

    RuntimeABindingRepository::RuntimeABindingRepository (juce::File transportRootIn, ReadClock readClockIn)
        : root (std::move (transportRootIn)), readClock (std::move (readClockIn))
    {
        if (! readClock) throw std::invalid_argument ("A binding read clock is required");
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
        return loadWithClock (identity, [nowMs] { return nowMs; });
    }

    std::optional<RuntimeABinding> RuntimeABindingRepository::loadCurrent (const RuntimeIdentity& identity) const
    {
        return loadWithClock (identity, readClock);
    }

    std::optional<RuntimeABinding> RuntimeABindingRepository::loadWithClock (
        const RuntimeIdentity& identity, const ReadClock& clock) const
    {
        KIRIN_REFERENCE_BINDING_READ_FOR_TEST (this);
        KIRIN_REFERENCE_BINDING_STAGE_FOR_TEST ("identity");
        if (! identity.valid() || ! runtimeId (identity.runtimeInstanceId)
            || ! canonicalUuid (identity.workId))
            return std::nullopt;
        juce::var value;
        if (! readBoundedJson (bindingFile (identity.runtimeInstanceId), value))
            return std::nullopt;
        KIRIN_REFERENCE_BINDING_STAGE_FOR_TEST ("schema");
        const auto* object = value.getDynamicObject();
        if (object == nullptr || ! exactProperties (*object, {
                "format", "version", "binding_id", "runtime_instance_id",
                "host_process_id", "work_id", "recording_id", "issued_at_ms",
                "lease_expires_at_ms" })
            || object->getProperty ("format") != "kirin_hypha_reference_a_binding"
            || object->getProperty ("version") != "1.0")
            return std::nullopt;

        const auto nowMs = clock();
        KIRIN_REFERENCE_BINDING_VALUES_FOR_TEST (nowMs,
            static_cast<std::int64_t> (object->getProperty ("issued_at_ms")),
            static_cast<std::int64_t> (object->getProperty ("lease_expires_at_ms")));
        KIRIN_REFERENCE_BINDING_STAGE_FOR_TEST ("clock");
        if (nowMs < 0 || nowMs > maximumSafeInteger) return std::nullopt;
        RuntimeABinding result;
        result.bindingId = object->getProperty ("binding_id").toString();
        result.runtimeInstanceId = object->getProperty ("runtime_instance_id").toString();
        result.workId = object->getProperty ("work_id").toString();
        result.recordingId = object->getProperty ("recording_id").toString();
        std::int64_t processId = 0;
        KIRIN_REFERENCE_BINDING_STAGE_FOR_TEST ("fields_or_time");
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
        KIRIN_REFERENCE_BINDING_ACCEPT_FOR_TEST();
        result.hostProcessId = static_cast<std::uint32_t> (processId);
        return result;
    }
}
