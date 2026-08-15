#include "PreDisplayPresence.h"

#include "PreDisplayProtocol.h"

namespace hypha::pre_display
{
    namespace
    {
        constexpr std::int64_t presenceLeaseMs = 1'500;

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

    bool writePresence (const juce::File& transportRoot,
                        const RuntimeIdentity& identity,
                        const ClockSnapshot& clock,
                        std::int64_t clockObservedAtMs,
                        std::int64_t nowMs)
    {
        if (transportRoot.getFullPathName().isEmpty()
            || ! safeId (identity.instanceId) || ! safeId (identity.projectUuid)
            || (identity.dawSessionUuid.isNotEmpty() && ! safeId (identity.dawSessionUuid))
            || identity.hostProcessId == 0)
            return false;
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_pre_display_presence");
        root->setProperty ("version", "1.0");
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

        auto capabilities = new juce::DynamicObject();
        capabilities->setProperty ("guide_protocol", "1.0");
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
        root->setProperty ("lease_expires_at_ms", nowMs + presenceLeaseMs);
        const auto file = transportRoot.getChildFile ("presence")
                                       .getChildFile (identity.instanceId + ".json");
        return replaceJsonAtomically (file, juce::var (root));
    }

    bool writeAcknowledgement (const juce::File& transportRoot,
                               const RuntimeIdentity& identity,
                               const GuideModel& guide,
                               const DisplaySnapshot& display,
                               std::int64_t nowMs)
    {
        const auto displayStatus = displayStatusName (display.status);
        if (transportRoot.getFullPathName().isEmpty()
            || ! safeId (identity.instanceId) || ! safeId (identity.projectUuid)
            || (identity.dawSessionUuid.isNotEmpty() && ! safeId (identity.dawSessionUuid))
            || identity.hostProcessId == 0 || ! guide.valid() || displayStatus.isEmpty()
            || display.guideId != guide.guideId || display.contentHash != guide.contentHash
            || display.payloadKind != guide.payloadKind)
            return false;

        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_pre_display_acknowledgement");
        root->setProperty ("version", "1.1");
        root->setProperty ("instance_id", identity.instanceId);
        root->setProperty ("host_process_id", static_cast<juce::int64> (identity.hostProcessId));
        root->setProperty ("project_uuid", identity.projectUuid);
        root->setProperty ("daw_session_uuid", identity.dawSessionUuid.isEmpty()
                                                   ? juce::var() : juce::var (identity.dawSessionUuid));
        root->setProperty ("group_id", guide.groupId);
        root->setProperty ("guide_id", guide.guideId);
        root->setProperty ("revision", static_cast<juce::int64> (guide.revision));
        root->setProperty ("content_hash", guide.contentHash);
        root->setProperty ("payload_kind", guide.payloadKind);
        root->setProperty ("display_status", displayStatus);
        root->setProperty ("observed_at_ms", nowMs);
        root->setProperty ("lease_expires_at_ms", nowMs + presenceLeaseMs);
        const auto file = transportRoot.getChildFile ("ack")
                                       .getChildFile (identity.instanceId + ".json");
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
}
