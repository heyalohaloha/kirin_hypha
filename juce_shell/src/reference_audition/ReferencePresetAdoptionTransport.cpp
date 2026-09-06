#include "ReferencePresetAdoptionTransport.h"

#include <regex>

#include "ReferenceRuntimeEventTransport.h"

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

        bool matches (const juce::String& value, const char* pattern)
        {
            return std::regex_match (value.toStdString(), std::regex (pattern));
        }

        bool uuid (const juce::String& value)
        {
            return matches (value, R"(^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$)");
        }

        bool uuidV4 (const juce::String& value)
        {
            return matches (value, R"(^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$)");
        }

        bool runtimeId (const juce::String& value)
        {
            return matches (value, R"(^[A-Za-z0-9._:-]{1,160}$)");
        }

        bool sha256 (const juce::String& value)
        {
            return matches (value, R"(^[a-f0-9]{64}$)");
        }

        bool exactProperties (const juce::DynamicObject& object,
                              std::initializer_list<const char*> expected)
        {
            if (object.getProperties().size() != static_cast<int> (expected.size()))
                return false;
            for (const auto* key : expected)
                if (! object.hasProperty (key)) return false;
            return true;
        }

        bool exactString (const juce::var& value, juce::String& output)
        {
            if (! value.isString()) return false;
            output = value.toString();
            return true;
        }

        bool exactInteger (const juce::var& value, std::int64_t minimum,
                           std::int64_t maximum, std::int64_t& output)
        {
            if (! value.isInt() && ! value.isInt64()) return false;
            output = static_cast<std::int64_t> (value);
            return output >= minimum && output <= maximum;
        }

        bool validReceipt (const RuntimeSourcePresetReceipt& receipt)
        {
            return uuidV4 (receipt.presetId) && uuidV4 (receipt.revisionId)
                && receipt.relativePath == "reference/presets/" + receipt.presetId
                                                + "/" + receipt.revisionId + ".v1.json"
                && sha256 (receipt.sha256) && receipt.bytes > 0
                && receipt.bytes <= 8 * 1024 * 1024;
        }

        juce::var receiptValue (const RuntimeSourcePresetReceipt& receipt)
        {
            auto value = new juce::DynamicObject();
            value->setProperty ("preset_id", receipt.presetId);
            value->setProperty ("revision_id", receipt.revisionId);
            value->setProperty ("relative_path", receipt.relativePath);
            value->setProperty ("sha256", receipt.sha256);
            value->setProperty ("bytes", receipt.bytes);
            return juce::var (value);
        }

        bool parseReceipt (const juce::var& value, RuntimeSourcePresetReceipt& output)
        {
            const auto* object = value.getDynamicObject();
            return object != nullptr && exactProperties (*object, {
                "preset_id", "revision_id", "relative_path", "sha256", "bytes" })
                && exactString (object->getProperty ("preset_id"), output.presetId)
                && exactString (object->getProperty ("revision_id"), output.revisionId)
                && exactString (object->getProperty ("relative_path"), output.relativePath)
                && exactString (object->getProperty ("sha256"), output.sha256)
                && exactInteger (object->getProperty ("bytes"), 1, 8 * 1024 * 1024,
                                 output.bytes)
                && validReceipt (output);
        }

        bool sameReceipt (const RuntimeSourcePresetReceipt& left,
                          const RuntimeSourcePresetReceipt& right)
        {
            return left.presetId == right.presetId
                && left.revisionId == right.revisionId
                && left.relativePath == right.relativePath
                && left.sha256 == right.sha256 && left.bytes == right.bytes;
        }

        bool fsyncDirectory (const juce::File& directory)
        {
           #if JUCE_WINDOWS
            juce::ignoreUnused (directory);
            return true;
           #else
            const int handle = ::open (directory.getFullPathName().toRawUTF8(), O_RDONLY);
            if (handle < 0) return false;
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

        bool writeImmutable (const juce::File& target, const juce::String& content)
        {
            const auto bytes = static_cast<std::int64_t> (content.getNumBytesAsUTF8());
            if (bytes < 1 || bytes > maximumPresetAdoptionBytes
                || ! target.getParentDirectory().createDirectory())
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

        bool existingMatches (const juce::File& file, const RuntimeIdentity& identity,
                              std::int64_t manifestRevision,
                              const RuntimeSourcePresetReceipt& sourceTemplate,
                              const RuntimeSourcePresetReceipt& sourcePreset)
        {
            if (! file.existsAsFile() || file.getSize() < 1
                || file.getSize() > maximumPresetAdoptionBytes)
                return false;
            const auto value = juce::JSON::parse (file);
            const auto* object = value.getDynamicObject();
            juce::String parsedRuntime, parsedWork;
            std::int64_t parsedProcess = 0, parsedManifest = 0, adoptedAt = 0;
            RuntimeSourcePresetReceipt parsedTemplate, parsedPreset;
            return object != nullptr && exactProperties (*object, {
                "format", "version", "runtime_instance_id", "host_process_id", "work_id",
                "manifest_revision", "source_template_artifact", "source_preset_artifact",
                "adopted_at_ms" })
                && object->getProperty ("format") == "kirin_hypha_reference_preset_adoption"
                && object->getProperty ("version") == "1.0"
                && exactString (object->getProperty ("runtime_instance_id"), parsedRuntime)
                && exactInteger (object->getProperty ("host_process_id"), 1, 4'294'967'295,
                                 parsedProcess)
                && exactString (object->getProperty ("work_id"), parsedWork)
                && exactInteger (object->getProperty ("manifest_revision"), 1,
                                 maxSafeJsonInteger, parsedManifest)
                && parseReceipt (object->getProperty ("source_template_artifact"), parsedTemplate)
                && parseReceipt (object->getProperty ("source_preset_artifact"), parsedPreset)
                && exactInteger (object->getProperty ("adopted_at_ms"), 0,
                                 maxSafeJsonInteger, adoptedAt)
                && parsedRuntime == identity.runtimeInstanceId
                && parsedProcess == static_cast<std::int64_t> (identity.hostProcessId)
                && parsedWork == identity.workId && parsedManifest == manifestRevision
                && sameReceipt (parsedTemplate, sourceTemplate)
                && sameReceipt (parsedPreset, sourcePreset);
        }
    }

    PresetAdoptionTransport::PresetAdoptionTransport (juce::File transportRootIn)
        : root (std::move (transportRootIn))
    {
    }

    juce::File PresetAdoptionTransport::adoptionFile (
        const RuntimeIdentity& identity, std::int64_t manifestRevision,
        const RuntimeSourcePresetReceipt& sourcePreset) const
    {
        if (! runtimeId (identity.runtimeInstanceId) || ! uuid (identity.workId)
            || identity.hostProcessId == 0 || manifestRevision < 1
            || manifestRevision > maxSafeJsonInteger || ! validReceipt (sourcePreset))
            return {};
        return root.getChildFile ("preset_adoptions")
            .getChildFile (identity.runtimeInstanceId)
            .getChildFile (identity.workId + "." + juce::String (manifestRevision) + "."
                           + sourcePreset.revisionId + ".json");
    }

    bool PresetAdoptionTransport::write (
        const RuntimeIdentity& identity, std::int64_t manifestRevision,
        const RuntimeSourcePresetReceipt& sourceTemplate,
        const RuntimeSourcePresetReceipt& sourcePreset, std::int64_t adoptedAtMs) const
    {
        const auto file = adoptionFile (identity, manifestRevision, sourcePreset);
        if (file == juce::File() || ! validReceipt (sourceTemplate)
            || sourceTemplate.presetId != sourcePreset.presetId
            || sourceTemplate.revisionId == sourcePreset.revisionId
            || adoptedAtMs < 0 || adoptedAtMs > maxSafeJsonInteger)
            return false;
        if (file.exists())
            return existingMatches (file, identity, manifestRevision, sourceTemplate, sourcePreset);

        auto object = new juce::DynamicObject();
        object->setProperty ("format", "kirin_hypha_reference_preset_adoption");
        object->setProperty ("version", "1.0");
        object->setProperty ("runtime_instance_id", identity.runtimeInstanceId);
        object->setProperty ("host_process_id", static_cast<juce::int64> (identity.hostProcessId));
        object->setProperty ("work_id", identity.workId);
        object->setProperty ("manifest_revision", manifestRevision);
        object->setProperty ("source_template_artifact", receiptValue (sourceTemplate));
        object->setProperty ("source_preset_artifact", receiptValue (sourcePreset));
        object->setProperty ("adopted_at_ms", adoptedAtMs);
        const auto content = RuntimeEventTransport::canonicalJson (juce::var (object));
        if (content.isEmpty()) return false;
        return writeImmutable (file, content)
            || existingMatches (file, identity, manifestRevision, sourceTemplate, sourcePreset);
    }
}
