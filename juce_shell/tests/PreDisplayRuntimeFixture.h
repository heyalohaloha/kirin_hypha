#pragma once
#include "TestTemporaryDirectory.h"
#include "../src/pre_display/PreDisplayClock.h"
#include "../src/pre_display/PreDisplayController.h"
#include "../src/pre_display/PreDisplayModel.h"
#include "../src/pre_display/PreDisplayPresence.h"
#include "../src/pre_display/PreDisplayProjection.h"
#include "../src/pre_display/PreDisplayProtocol.h"
#include "../src/pre_display/PreDisplayRepository.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "../src/AtomicFile.h"
#include <juce_cryptography/juce_cryptography.h>
#include "TestPathBudget.h"
namespace pre = hypha::pre_display;

namespace
{
    const juce::String hashA =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const juce::String hashB =
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    void require (bool condition, const char* message)
    {
        if (! condition)
        {
            std::cerr << "PRE display runtime test failed: " << message << '\n';
            std::exit (1);
        }
    }

    bool exactDouble (double left, double right)
    {
        return std::memcmp (&left, &right, sizeof (double)) == 0;
    }

    juce::File leaseFileForInstance (const juce::File& directory,
                                     const juce::String& instanceId)
    {
        for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, "*.json"))
        {
            const auto parsed = juce::JSON::parse (file);
            if (parsed.getDynamicObject() != nullptr
                && parsed.getDynamicObject()->getProperty ("instance_id") == instanceId)
                return file;
        }
        return {};
    }

    juce::String testUuid (const juce::String& seed)
    {
        const auto digest = juce::MD5 (seed.toRawUTF8(), seed.getNumBytesAsUTF8()).toHexString();
        return digest.substring (0, 8) + "-" + digest.substring (8, 12) + "-"
             + digest.substring (12, 16) + "-" + digest.substring (16, 20) + "-"
             + digest.substring (20, 32);
    }

    juce::var source (const juce::String& ref, const juce::String& label)
    {
        auto object = new juce::DynamicObject();
        object->setProperty ("source_ref", ref);
        object->setProperty ("display_label", label);
        object->setProperty ("duration_ns", static_cast<juce::int64> (4'000'000'000));
        return juce::var (object);
    }

    juce::var maskingGuide (const juce::String& guideId,
                            const juce::String& contentHash,
                            bool includeMeasurementState,
                            bool includeFrequencyBasis,
                            bool includeInterval)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_pre_display_guide");
        root->setProperty ("version", "1.0");
        root->setProperty ("guide_id", testUuid (guideId));
        root->setProperty ("created_at", "2026-08-15T12:00:00.000Z");
        root->setProperty ("content_hash", contentHash);
        root->setProperty ("revision", static_cast<juce::int64> (1));

        auto producer = new juce::DynamicObject();
        producer->setProperty ("session_id", "22222222-2222-4222-8222-222222222222");
        producer->setProperty ("work_id", juce::var());
        producer->setProperty ("report_id", juce::var());
        root->setProperty ("producer", juce::var (producer));

        auto target = new juce::DynamicObject();
        target->setProperty ("group_id", "kirin_os");
        target->setProperty ("selection_mode", "all_pre_instances");
        root->setProperty ("target", juce::var (target));

        auto timeBasis = new juce::DynamicObject();
        timeBasis->setProperty ("kind", "source_to_project_offset");
        timeBasis->setProperty ("unit", "nanoseconds");
        timeBasis->setProperty ("interval_convention", "half_open");
        timeBasis->setProperty ("alignment_method", "project_zero");
        timeBasis->setProperty ("source_zero_project_ns", static_cast<juce::int64> (0));
        root->setProperty ("time_basis", juce::var (timeBasis));

        juce::Array<juce::var> sourceSet;
        sourceSet.add (source ("source_a", "Source A"));
        sourceSet.add (source ("source_b", "Source B"));
        root->setProperty ("source_set", juce::var (sourceSet));

        auto payload = new juce::DynamicObject();
        payload->setProperty ("kind", "masking");
        payload->setProperty ("pair_key", "source_a:source_b");
        if (includeMeasurementState)
            payload->setProperty ("measurement_state", "measured");
        juce::Array<juce::var> sourceOrder;
        sourceOrder.add ("source_a");
        sourceOrder.add ("source_b");
        payload->setProperty ("source_order", juce::var (sourceOrder));

        juce::Array<juce::var> intervals;
        if (includeInterval)
        {
            auto interval = new juce::DynamicObject();
            interval->setProperty ("item_id", "mask_1");
            interval->setProperty ("start_ns", static_cast<juce::int64> (1'000'000'000));
            interval->setProperty ("end_ns", static_cast<juce::int64> (2'000'000'000));
            interval->setProperty ("frequency_state", "measured");
            if (includeFrequencyBasis)
                interval->setProperty ("frequency_basis", "bark24_edges");
            auto band = new juce::DynamicObject();
            band->setProperty ("low_hz", 100.0);
            band->setProperty ("high_hz", 200.0);
            interval->setProperty ("band", juce::var (band));
            intervals.add (juce::var (interval));
        }
        payload->setProperty ("intervals", juce::var (intervals));
        root->setProperty ("payload", juce::var (payload));
        return juce::var (root);
    }

    juce::var inspectGuide()
    {
        auto value = maskingGuide ("inspect_guide", hashB, false, false, false);
        auto* root = value.getDynamicObject();
        auto* payload = root->getProperty ("payload").getDynamicObject();
        payload->setProperty ("kind", "inspect");
        payload->removeProperty ("pair_key");
        payload->removeProperty ("source_order");
        payload->removeProperty ("intervals");
        payload->setProperty ("source_ref", "source_a");
        payload->setProperty ("focus_event_id", "event_1");
        auto event = new juce::DynamicObject();
        event->setProperty ("item_id", "event_1");
        event->setProperty ("start_ns", static_cast<juce::int64> (1'000'000'000));
        event->setProperty ("end_ns", static_cast<juce::int64> (2'000'000'000));
        event->setProperty ("type_id", "true_peak");
        event->setProperty ("source_ref", "source_a");
        event->setProperty ("display_label", "True Peak");
        event->setProperty ("channel_key", "mix");
        juce::Array<juce::var> events;
        events.add (juce::var (event));
        payload->setProperty ("events", juce::var (events));
        return value;
    }

    juce::var exactInspectGuide (const juce::String& workId,
                                 const juce::String& bindingId,
                                 const juce::String& runtimeInstanceId)
    {
        auto value = inspectGuide();
        auto* root = value.getDynamicObject();
        root->setProperty ("version", "2.0");
        auto* producer = root->getProperty ("producer").getDynamicObject();
        producer->setProperty ("work_id", workId);
        producer->setProperty ("source_sha256_file", hashA);
        auto target = new juce::DynamicObject();
        target->setProperty ("group_id", "kirin_os");
        target->setProperty ("selection_mode", "exact_pre_binding");
        target->setProperty ("work_id", workId);
        target->setProperty ("binding_id", bindingId);
        target->setProperty ("runtime_instance_id", runtimeInstanceId);
        root->setProperty ("target", juce::var (target));
        return value;
    }

    juce::var exactMaskingGuide (const juce::String& workId,
                                 const juce::String& bindingId,
                                 const juce::String& runtimeInstanceId)
    {
        auto value = maskingGuide ("exact_masking", hashA, true, true, true);
        auto* root = value.getDynamicObject();
        root->setProperty ("version", "2.0");
        auto* producer = root->getProperty ("producer").getDynamicObject();
        producer->setProperty ("work_id", workId);
        producer->setProperty ("source_sha256_file", hashB);
        auto target = new juce::DynamicObject();
        target->setProperty ("group_id", "kirin_os");
        target->setProperty ("selection_mode", "exact_pre_binding");
        target->setProperty ("work_id", workId);
        target->setProperty ("binding_id", bindingId);
        target->setProperty ("runtime_instance_id", runtimeInstanceId);
        root->setProperty ("target", juce::var (target));

        auto* payload = root->getProperty ("payload").getDynamicObject();
        auto selection = new juce::DynamicObject();
        selection->setProperty ("selection_id", "review_0001");
        selection->setProperty ("start_ns", static_cast<juce::int64> (1'000'000'000));
        selection->setProperty ("end_ns", static_cast<juce::int64> (2'000'000'000));
        auto focusBand = new juce::DynamicObject();
        focusBand->setProperty ("low_hz", 100.0);
        focusBand->setProperty ("high_hz", 200.0);
        selection->setProperty ("focus_band", juce::var (focusBand));
        juce::Array<juce::var> selections;
        selections.add (juce::var (selection));
        payload->setProperty ("review_selections", juce::var (selections));
        payload->getProperty ("intervals").getArray()->getReference (0)
            .getDynamicObject()->setProperty ("selection_ref", "review_0001");
        return value;
    }

    bool writeJson (const juce::File& file, const juce::var& value)
    {
        return file.getParentDirectory().createDirectory()
            && hypha::storage::writeText (file, juce::JSON::toString (value, true) + "\n");
    }

    juce::var activePointer (const juce::File& guideFile,
                             const juce::String& artifactHash,
                             const juce::String& guideId,
                             const juce::String& contentHash,
                             const juce::String& payloadKind,
                             std::int64_t revision)
    {
        auto pointer = new juce::DynamicObject();
        pointer->setProperty ("format", "kirin_pre_display_active");
        pointer->setProperty ("version", "1.0");
        pointer->setProperty ("guide_file", guideFile.getFileName());
        pointer->setProperty ("artifact_sha256", artifactHash);
        pointer->setProperty ("guide_id", guideId);
        pointer->setProperty ("content_hash", contentHash);
        pointer->setProperty ("group_id", "kirin_os");
        pointer->setProperty ("payload_kind", payloadKind);
        pointer->setProperty ("revision", static_cast<juce::int64> (revision));
        pointer->setProperty ("activated_at", "2026-08-15T12:00:00.000Z");
        return juce::var (pointer);
    }

    juce::var exactActivePointer (const juce::File& guideFile,
                                  const juce::String& artifactHash,
                                  const juce::String& guideId,
                                  const juce::String& contentHash,
                                  const juce::String& workId,
                                  const juce::String& bindingId,
                                  const juce::String& runtimeInstanceId,
                                  pre::GuideTargetRole role = pre::GuideTargetRole::pre)
    {
        auto pointer = new juce::DynamicObject();
        const bool post = role == pre::GuideTargetRole::post;
        pointer->setProperty ("format", "kirin_pre_display_active");
        pointer->setProperty ("version", post ? "3.0" : "2.0");
        pointer->setProperty ("group_id", "kirin_os");
        if (post)
            pointer->setProperty ("target_role", "post");
        pointer->setProperty ("work_id", workId);
        pointer->setProperty ("binding_id", bindingId);
        pointer->setProperty ("runtime_instance_id", runtimeInstanceId);
        pointer->setProperty ("guide_id", guideId);
        pointer->setProperty ("revision", static_cast<juce::int64> (1));
        pointer->setProperty ("content_hash", contentHash);
        pointer->setProperty ("artifact_sha256", artifactHash);
        pointer->setProperty ("guide_file", guideFile.getFileName());
        pointer->setProperty ("payload_kind", "inspect");
        pointer->setProperty ("activated_at", "2026-08-15T12:00:00.000Z");
        return juce::var (pointer);
    }

    juce::var connectionRequest (const juce::String& workId,
                                 const juce::String& bindingId,
                                 std::int64_t nowMs,
                                 pre::GuideTargetRole role = pre::GuideTargetRole::pre)
    {
        auto request = new juce::DynamicObject();
        const bool post = role == pre::GuideTargetRole::post;
        request->setProperty ("format", "kirin_pre_display_connection_request");
        request->setProperty ("version", post ? "2.0" : "1.0");
        if (post)
            request->setProperty ("target_role", "post");
        request->setProperty ("binding_id", bindingId);
        request->setProperty ("work_id", workId);
        request->setProperty ("work_title", "Exact Work");
        request->setProperty ("observed_at_ms", nowMs);
        request->setProperty ("expires_at_ms", nowMs + 300'000);
        return juce::var (request);
    }

    juce::File publishGuide (const juce::File& root, const juce::var& guide,
                             const juce::String& artifactOverride = {})
    {
        const auto* object = guide.getDynamicObject();
        const auto guideId = pre::objectString (*object, "guide_id");
        const auto guideFile = root.getChildFile ("guides").getChildFile (guideId + ".json");
        require (writeJson (guideFile, guide), "write guide fixture");
        const auto artifactHash = artifactOverride.isNotEmpty()
                                    ? artifactOverride : juce::SHA256 (guideFile).toHexString();
        const auto payloadKind = object->getProperty ("payload").getDynamicObject()
                                       ->getProperty ("kind").toString();
        require (writeJson (root.getChildFile ("active").getChildFile ("kirin_os.json"),
                            activePointer (guideFile, artifactHash, guideId,
                                           pre::objectString (*object, "content_hash"),
                                           payloadKind, 1)),
                 "write active pointer fixture");
        return guideFile;
    }

    juce::var clearAuthority (const juce::String& groupId)
    {
        auto clear = new juce::DynamicObject();
        clear->setProperty ("format", "kirin_pre_display_retired");
        clear->setProperty ("version", "1.0");
        clear->setProperty ("scope", "group");
        clear->setProperty ("group_id", groupId);
        clear->setProperty ("retired_at", "2026-08-15T12:00:00.000Z");
        return juce::var (clear);
    }
}

