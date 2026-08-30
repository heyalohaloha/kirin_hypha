#include "PreDisplayPresence.h"

#include "PreDisplayProtocol.h"

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <unistd.h>
#endif

namespace hypha::pre_display
{
    namespace
    {
        constexpr std::int64_t presenceLeaseMs = 1'500;

        std::uint32_t currentProcessId() noexcept
        {
           #if JUCE_WINDOWS
            return static_cast<std::uint32_t> (::GetCurrentProcessId());
           #else
            return static_cast<std::uint32_t> (::getpid());
           #endif
        }

        bool validLeaseNow (std::int64_t nowMs)
        {
            return nowMs >= 0 && nowMs <= maxSafeJsonInteger - presenceLeaseMs;
        }

        bool validIdentity (const juce::File& transportRoot, const RuntimeIdentity& identity)
        {
            return transportRoot.getFullPathName().isNotEmpty()
                && safeId (identity.instanceId) && safeId (identity.projectUuid)
                && (identity.dawSessionUuid.isEmpty() || safeId (identity.dawSessionUuid))
                && identity.hostProcessId != 0
                && (identity.runtimeInstanceId.isEmpty() || safeId (identity.runtimeInstanceId))
                && ((identity.workId.isEmpty() && identity.bindingId.isEmpty())
                    || (safeId (identity.workId) && safeId (identity.bindingId)));
        }

        bool validReceiptIdentity (const GuideReceipt& receipt)
        {
            return safeId (receipt.groupId) && safeId (receipt.guideId)
                && receipt.revision > 0 && receipt.revision <= maxSafeJsonInteger
                && safeHash (receipt.contentHash)
                && (receipt.payloadKind == "masking" || receipt.payloadKind == "inspect")
                && (receipt.runtimeInstanceId.isEmpty()
                    || (safeId (receipt.runtimeInstanceId) && safeId (receipt.workId)
                        && safeId (receipt.bindingId)));
        }

        void addRuntimeIdentity (juce::DynamicObject& root, const RuntimeIdentity& identity)
        {
            if (identity.runtimeInstanceId.isNotEmpty())
                root.setProperty ("runtime_instance_id", identity.runtimeInstanceId);
            root.setProperty ("instance_id", identity.instanceId);
            root.setProperty ("host_process_id", static_cast<juce::int64> (identity.hostProcessId));
            root.setProperty ("project_uuid", identity.projectUuid);
            root.setProperty ("daw_session_uuid", identity.dawSessionUuid.isEmpty()
                                                     ? juce::var() : juce::var (identity.dawSessionUuid));
            if (identity.runtimeInstanceId.isNotEmpty())
            {
                root.setProperty ("work_id", identity.workId.isEmpty()
                                                   ? juce::var() : juce::var (identity.workId));
                root.setProperty ("binding_id", identity.bindingId.isEmpty()
                                                      ? juce::var() : juce::var (identity.bindingId));
            }
        }

        void addLease (juce::DynamicObject& root, std::int64_t nowMs)
        {
            root.setProperty ("observed_at_ms", nowMs);
            root.setProperty ("lease_expires_at_ms", nowMs + presenceLeaseMs);
        }

        void addGuideIdentity (juce::DynamicObject& root, const GuideReceipt& receipt)
        {
            root.setProperty ("group_id", receipt.groupId);
            root.setProperty ("guide_id", receipt.guideId);
            root.setProperty ("revision", static_cast<juce::int64> (receipt.revision));
            root.setProperty ("content_hash", receipt.contentHash);
            root.setProperty ("payload_kind", receipt.payloadKind);
            if (receipt.runtimeInstanceId.isNotEmpty())
            {
                root.setProperty ("work_id", receipt.workId);
                root.setProperty ("binding_id", receipt.bindingId);
                root.setProperty ("runtime_instance_id", receipt.runtimeInstanceId);
            }
        }

        bool replaceJsonAtomically (const juce::File& target, const juce::var& value)
        {
            if (! target.getParentDirectory().createDirectory())
                return false;
            juce::TemporaryFile temporary (target);
            {
                auto stream = temporary.getFile().createOutputStream();
                if (stream == nullptr || ! stream->openedOk())
                    return false;
                if (! stream->writeText (juce::JSON::toString (value, true) + "\n",
                                         false, false, "\n"))
                    return false;
                stream->flush();
                if (stream->getStatus().failed())
                    return false;
            }
            return temporary.overwriteTargetFileWithTemporary();
        }

        juce::String clockSourceName (ClockSource source)
        {
            switch (source)
            {
                case ClockSource::projectTimeline: return "project_timeline";
                case ClockSource::audioRenderTimeline: return "audio_render_timeline";
                case ClockSource::unknown: break;
            }
            return "unknown";
        }

        juce::String displayStatusName (DisplayStatus status)
        {
            switch (status)
            {
                case DisplayStatus::received: return "received";
                case DisplayStatus::waitingForProjectClock: return "waiting_for_project_clock";
                case DisplayStatus::active: return "active";
                case DisplayStatus::next: return "next";
                case DisplayStatus::end: return "end";
                case DisplayStatus::none: break;
            }
            return {};
        }
    }

    RuntimeLease prepareRuntimeLease (const juce::File& transportRoot,
                                      RuntimeIdentity requested,
                                      const RuntimeIdentity& current,
                                      bool alreadyConfigured)
    {
        RuntimeLease lease;
        if (transportRoot.getFullPathName().isEmpty()
            || ! safeId (requested.instanceId) || ! safeId (requested.projectUuid)
            || (requested.dawSessionUuid.isNotEmpty() && ! safeId (requested.dawSessionUuid)))
            return lease;
        if (requested.hostProcessId == 0)
            requested.hostProcessId = currentProcessId();

        const bool sameOpenInstance = alreadyConfigured
            && current.instanceId == requested.instanceId
            && current.projectUuid == requested.projectUuid
            && current.dawSessionUuid == requested.dawSessionUuid
            && current.hostProcessId == requested.hostProcessId;
        if (! safeId (requested.runtimeInstanceId))
            requested.runtimeInstanceId = sameOpenInstance && safeId (current.runtimeInstanceId)
                ? current.runtimeInstanceId : juce::Uuid().toString();
        if (sameOpenInstance && requested.workId.isEmpty() && requested.bindingId.isEmpty())
        {
            requested.workId = current.workId;
            requested.bindingId = current.bindingId;
        }
        if (! safeId (requested.runtimeInstanceId))
            return lease;

        lease.identity = std::move (requested);
        lease.presenceFile = transportRoot.getChildFile ("presence")
                                          .getChildFile (lease.identity.runtimeInstanceId + ".json");
        lease.acknowledgementFile = transportRoot.getChildFile ("ack")
                                               .getChildFile (lease.identity.runtimeInstanceId + ".json");
        lease.capabilityFile = transportRoot.getChildFile ("capability")
                                            .getChildFile (lease.identity.runtimeInstanceId + ".json");
        return lease;
    }

    bool writePresence (const juce::File& transportRoot,
                        const RuntimeIdentity& identity,
                        const ClockSnapshot& clock,
                        std::int64_t clockObservedAtMs,
                        std::int64_t nowMs)
    {
        if (! validIdentity (transportRoot, identity) || ! validLeaseNow (nowMs))
            return false;
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_pre_display_presence");
        const bool exact = identity.runtimeInstanceId.isNotEmpty();
        root->setProperty ("version", exact ? "2.0" : "1.0");
        if (exact)
            root->setProperty ("runtime_instance_id", identity.runtimeInstanceId);
        root->setProperty ("instance_id", identity.instanceId);
        root->setProperty ("name", identity.name.substring (0, 64));
        root->setProperty ("plugin_version", identity.pluginVersion);
        root->setProperty ("plugin_format", identity.pluginFormat);
        root->setProperty ("platform", identity.platform);
        root->setProperty ("architecture", identity.architecture);
        root->setProperty ("host_process_id", static_cast<juce::int64> (identity.hostProcessId));
        root->setProperty ("project_uuid", identity.projectUuid);
        root->setProperty ("daw_session_uuid", identity.dawSessionUuid.isEmpty()
                                                   ? juce::var() : juce::var (identity.dawSessionUuid));
        if (exact)
        {
            root->setProperty ("work_id", identity.workId.isEmpty()
                                               ? juce::var() : juce::var (identity.workId));
            root->setProperty ("binding_id", identity.bindingId.isEmpty()
                                                  ? juce::var() : juce::var (identity.bindingId));
        }

        auto capabilities = new juce::DynamicObject();
        capabilities->setProperty ("guide_protocol", exact ? "2.0" : "1.0");
        if (exact)
            capabilities->setProperty ("guide_acknowledgement", "2.0");
        capabilities->setProperty ("project_clock", true);
        capabilities->setProperty ("audio_alignment", false);
        root->setProperty ("capabilities", juce::var (capabilities));

        auto clockObject = new juce::DynamicObject();
        clockObject->setProperty ("generation", static_cast<juce::int64> (clock.generation));
        clockObject->setProperty ("position_samples", juce::String (clock.positionSamples));
        clockObject->setProperty ("sample_rate", clock.sampleRate);
        clockObject->setProperty ("block_frames", static_cast<juce::int64> (clock.blockFrames));
        clockObject->setProperty ("playing", clock.playing);
        clockObject->setProperty ("source", clockSourceName (clock.source));
        clockObject->setProperty ("observed_at_ms", clockObservedAtMs > 0
                                                       ? clockObservedAtMs : nowMs);
        root->setProperty ("clock", juce::var (clockObject));
        if (exact)
            root->setProperty ("observed_at_ms", nowMs);
        root->setProperty ("lease_expires_at_ms", nowMs + presenceLeaseMs);
        const auto file = transportRoot.getChildFile ("presence")
                                       .getChildFile ((exact ? identity.runtimeInstanceId : identity.instanceId) + ".json");
        return replaceJsonAtomically (file, juce::var (root));
    }

    bool writeCapability (const juce::File& transportRoot,
                          const RuntimeIdentity& identity,
                          std::int64_t nowMs)
    {
        if (! validIdentity (transportRoot, identity) || ! validLeaseNow (nowMs))
            return false;
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_pre_display_capability");
        const bool exact = identity.runtimeInstanceId.isNotEmpty();
        root->setProperty ("version", exact ? "2.0" : "1.0");
        addRuntimeIdentity (*root, identity);
        root->setProperty ("guide_protocol", exact ? "2.0" : "1.0");
        root->setProperty ("acknowledgement_protocol", exact ? "2.0" : "1.2");
        addLease (*root, nowMs);
        const auto file = transportRoot.getChildFile ("capability")
                                       .getChildFile ((exact ? identity.runtimeInstanceId : identity.instanceId) + ".json");
        return replaceJsonAtomically (file, juce::var (root));
    }

    bool writeAcknowledgement (const juce::File& transportRoot,
                               const RuntimeIdentity& identity,
                               const GuideModel& guide,
                               const DisplaySnapshot& display,
                               std::int64_t nowMs)
    {
        const auto displayStatus = displayStatusName (display.status);
        GuideReceipt receipt;
        receipt.groupId = guide.groupId;
        receipt.workId = guide.workId;
        receipt.bindingId = guide.bindingId;
        receipt.runtimeInstanceId = guide.runtimeInstanceId;
        receipt.guideId = guide.guideId;
        receipt.revision = guide.revision;
        receipt.contentHash = guide.contentHash;
        receipt.payloadKind = guide.payloadKind;
        if (! validIdentity (transportRoot, identity) || ! validLeaseNow (nowMs)
            || ! guide.valid() || ! validReceiptIdentity (receipt) || displayStatus.isEmpty()
            || (guide.protocolVersion == "2.0"
                && (identity.runtimeInstanceId != guide.runtimeInstanceId
                    || identity.workId != guide.workId || identity.bindingId != guide.bindingId))
            || display.guideId != guide.guideId || display.contentHash != guide.contentHash
            || display.payloadKind != guide.payloadKind)
            return false;

        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_pre_display_acknowledgement");
        const bool exact = guide.protocolVersion == "2.0";
        root->setProperty ("version", exact ? "2.0" : "1.2");
        addRuntimeIdentity (*root, identity);
        addGuideIdentity (*root, receipt);
        root->setProperty ("receipt_status", "accepted");
        root->setProperty ("display_status", displayStatus);
        root->setProperty ("rejection_code", juce::var());
        addLease (*root, nowMs);
        const auto file = transportRoot.getChildFile ("ack")
                                       .getChildFile ((exact ? identity.runtimeInstanceId : identity.instanceId) + ".json");
        return replaceJsonAtomically (file, juce::var (root));
    }

    bool writeRejectedAcknowledgement (const juce::File& transportRoot,
                                       const RuntimeIdentity& identity,
                                       const GuideReceipt& receipt,
                                       std::int64_t nowMs)
    {
        if (! validIdentity (transportRoot, identity) || ! validLeaseNow (nowMs)
            || receipt.state != GuideRefreshState::rejected || ! receipt.hasGuideIdentity()
            || ! validReceiptIdentity (receipt))
            return false;
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_pre_display_acknowledgement");
        const bool exact = receipt.runtimeInstanceId.isNotEmpty();
        root->setProperty ("version", exact ? "2.0" : "1.2");
        addRuntimeIdentity (*root, identity);
        addGuideIdentity (*root, receipt);
        root->setProperty ("receipt_status", "rejected");
        root->setProperty ("display_status", juce::var());
        root->setProperty ("rejection_code", "guide_contract_rejected");
        addLease (*root, nowMs);
        const auto file = transportRoot.getChildFile ("ack")
                                       .getChildFile ((exact ? identity.runtimeInstanceId : identity.instanceId) + ".json");
        return replaceJsonAtomically (file, juce::var (root));
    }

    void removePresence (const juce::File& file)
    {
        if (file != juce::File() && file.existsAsFile())
            file.deleteFile();
    }

    void removeAcknowledgement (const juce::File& file)
    {
        if (file != juce::File() && file.existsAsFile())
            file.deleteFile();
    }

    void removeCapability (const juce::File& file)
    {
        if (file != juce::File() && file.existsAsFile())
            file.deleteFile();
    }
}
