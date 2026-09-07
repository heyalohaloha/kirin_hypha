#include "ReferenceAuditionLease.h"

#include "ReferenceAuditionProtocol.h"

namespace hypha::reference_audition
{
    namespace
    {
        constexpr std::int64_t leaseDurationMs = 1'500;
        constexpr std::int64_t maxSafeJsonInteger = 9'007'199'254'740'991;

        bool validNow (std::int64_t nowMs)
        {
            return nowMs >= 0 && nowMs <= maxSafeJsonInteger - leaseDurationMs;
        }

        bool replaceJsonAtomically (const juce::File& target, const juce::var& value)
        {
            if (! target.getParentDirectory().createDirectory())
                return false;
            juce::TemporaryFile temporary (target);
            {
                auto stream = temporary.getFile().createOutputStream();
                if (stream == nullptr || ! stream->openedOk()
                    || ! stream->writeText (juce::JSON::toString (value, true) + "\n",
                                            false, false, "\n"))
                    return false;
                stream->flush();
                if (stream->getStatus().failed())
                    return false;
            }
            return temporary.overwriteTargetFileWithTemporary();
        }

    }

    RuntimeFiles runtimeFiles (const juce::File& transportRoot,
                               const RuntimeIdentity& identity)
    {
        if (transportRoot == juce::File() || ! identity.valid())
            return {};
        return {
            transportRoot.getChildFile ("capabilities")
                         .getChildFile (identity.runtimeInstanceId + ".json"),
            transportRoot.getChildFile ("acknowledgements")
                         .getChildFile (identity.runtimeInstanceId + ".json"),
        };
    }

    bool writeCapability (const juce::File& transportRoot,
                          const RuntimeIdentity& identity,
                          std::int64_t nowMs)
    {
        const auto files = runtimeFiles (transportRoot, identity);
        if (! files.valid() || ! validNow (nowMs))
            return false;
        auto ordered = new juce::DynamicObject();
        ordered->setProperty ("format", "kirin_hypha_ab_capability");
        ordered->setProperty ("version", "1.0");
        ordered->setProperty ("runtime_instance_id", identity.runtimeInstanceId);
        ordered->setProperty ("host_process_id", static_cast<juce::int64> (identity.hostProcessId));
        ordered->setProperty ("work_id", identity.workId);
        ordered->setProperty ("target_role", "post");
        ordered->setProperty ("preparation_protocol", "1.2");
        ordered->setProperty ("acknowledgement_protocol", "1.0");
        ordered->setProperty ("observed_at_ms", nowMs);
        ordered->setProperty ("lease_expires_at_ms", nowMs + leaseDurationMs);
        return replaceJsonAtomically (files.capability, juce::var (ordered));
    }

    bool writeAcknowledgement (const juce::File& transportRoot,
                               const RuntimeIdentity& identity,
                               const Preparation& preparation,
                               const juce::String& rejectionCode,
                               std::int64_t nowMs)
    {
        const auto files = runtimeFiles (transportRoot, identity);
        if (! files.valid() || ! preparation.valid() || preparation.workId != identity.workId
            || ! validNow (nowMs) || (rejectionCode.isNotEmpty() && ! safeId (rejectionCode)))
            return false;
        auto object = new juce::DynamicObject();
        object->setProperty ("format", "kirin_hypha_ab_acknowledgement");
        object->setProperty ("version", "1.0");
        object->setProperty ("runtime_instance_id", identity.runtimeInstanceId);
        object->setProperty ("host_process_id", static_cast<juce::int64> (identity.hostProcessId));
        object->setProperty ("work_id", identity.workId);
        object->setProperty ("target_role", "post");
        object->setProperty ("preparation_id", preparation.preparationId);
        object->setProperty ("source_sha256_file", preparation.sourceSha256);
        object->setProperty ("receipt_status", rejectionCode.isEmpty() ? "accepted" : "rejected");
        object->setProperty ("rejection_code", rejectionCode.isEmpty()
                                                    ? juce::var() : juce::var (rejectionCode));
        object->setProperty ("observed_at_ms", nowMs);
        object->setProperty ("lease_expires_at_ms", nowMs + leaseDurationMs);
        return replaceJsonAtomically (files.acknowledgement, juce::var (object));
    }

    void removeRuntimeFiles (const RuntimeFiles& files)
    {
        if (files.capability.existsAsFile())
            files.capability.deleteFile();
        if (files.acknowledgement.existsAsFile())
            files.acknowledgement.deleteFile();
    }
}
