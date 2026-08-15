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

#include <juce_cryptography/juce_cryptography.h>

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

    bool writeJson (const juce::File& file, const juce::var& value)
    {
        return file.getParentDirectory().createDirectory()
            && file.replaceWithText (juce::JSON::toString (value, true) + "\n");
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

int main()
{
    const auto fixtureFile = juce::File::getCurrentWorkingDirectory()
        .getChildFile ("tests").getChildFile ("fixtures")
        .getChildFile ("masking_measured_bark_guide.v1.json");
    require (fixtureFile.existsAsFile(), "locate the shared producer-consumer guide fixture");
    require (juce::SHA256 (fixtureFile).toHexString()
                 == "3875787a00fec525247063ef0c654ae70968457545e38f890baf334065d748c3",
             "retain byte-exact parity with the Kirin OS fixture");
    const auto fixtureValue = juce::JSON::parse (fixtureFile);
    pre::GuideModel fixtureModel;
    require (fixtureValue.getDynamicObject() != nullptr
             && pre::parseArtifactVerifiedGuideModel (
                    *fixtureValue.getDynamicObject(), "fixture", fixtureModel),
             "parse the exact Kirin OS MASKING fixture");
    pre::ClockSnapshot fixtureClock;
    fixtureClock.generation = 1;
    fixtureClock.positionSamples = 72'000;
    fixtureClock.sampleRate = 48'000.0;
    fixtureClock.playing = true;
    fixtureClock.source = pre::ClockSource::projectTimeline;
    const auto fixtureDisplay = pre::projectDisplay (fixtureModel, fixtureClock, 1'000, 1'000);
    require (fixtureDisplay.status == pre::DisplayStatus::active
             && fixtureDisplay.sectionActive
             && fixtureDisplay.primary.contains (juce::CharPointer_UTF8 ("100\xe2\x80\x93" "200 Hz")),
             "apply the fixture's source-to-project offset and exact Bark band");
    const auto inspectFixtureFile = juce::File::getCurrentWorkingDirectory()
        .getChildFile ("tests").getChildFile ("fixtures")
        .getChildFile ("inspect_decimal_guide.v1.json");
    require (juce::SHA256 (inspectFixtureFile).toHexString()
                 == "9c8af9d7b99ccba7c5e35a19bab5c0ff2d56c8484d37e3cf58d7c1737d433d1c",
             "retain byte-exact INSPECT fixture parity with Kirin OS");
    const auto inspectFixtureValue = juce::JSON::parse (inspectFixtureFile);
    pre::GuideModel inspectFixtureModel;
    require (inspectFixtureValue.getDynamicObject() != nullptr
             && pre::parseArtifactVerifiedGuideModel (*inspectFixtureValue.getDynamicObject(),
                                                      "inspect-fixture", inspectFixtureModel)
             && inspectFixtureModel.items.size() == 1
             && exactDouble (inspectFixtureModel.items.front().lowHz, 0.12345678901234566)
             && exactDouble (inspectFixtureModel.items.front().highHz, 12'345.678901234567)
             && inspectFixtureModel.items.front().label
                    == juce::CharPointer_UTF8 ("\xe3\x83\x9e\xe3\x82\xa6\xe3\x82\xb9"
                                               "\xe3\x82\xaf\xe3\x83\xaa\xe3\x83\x83\xe3\x82\xaf"),
             "parse exact Unicode labels and difficult production INSPECT decimals");

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("kirin-pre-display-runtime", {}, false);
    require (root.createDirectory(), "create isolated transport root");

    const auto fixtureArtifact = root.getChildFile ("guides").getChildFile (
        "masking_measured_bark_guide.v1.json");
    require (fixtureArtifact.getParentDirectory().createDirectory()
             && fixtureFile.copyFileTo (fixtureArtifact), "copy the exact guide fixture");
    require (writeJson (root.getChildFile ("active").getChildFile ("kirin_os.json"),
                        activePointer (
                            fixtureArtifact,
                            "3875787a00fec525247063ef0c654ae70968457545e38f890baf334065d748c3",
                            "11111111-1111-4111-8111-111111111111",
                            "76c628baf4188b63ea16533659ef7763070082fda622908f9ae1a44bc998336f",
                            "masking", 7)),
             "write fixture pointer");
    pre::GuideModel fixtureFromRepository;
    const auto fixtureReceipt = pre::GuideRepository (root).refresh (fixtureFromRepository);
    require (fixtureReceipt.state == pre::GuideRefreshState::accepted
             && fixtureFromRepository.guideId == fixtureModel.guideId
             && fixtureFromRepository.contentHash == fixtureModel.contentHash,
             "load the exact cross-boundary fixture through the file repository");

    const auto measured = maskingGuide ("masking_guide", hashA, true, true, true);
    publishGuide (root, measured);
    pre::GuideModel retained;
    pre::GuideRepository repository (root);
    const auto measuredReceipt = repository.refresh (retained);
    require (measuredReceipt.state == pre::GuideRefreshState::accepted && retained.valid(),
             "load an artifact-verified masking guide");
    require (retained.items.size() == 1, "retain one masking interval");
    require (retained.items.front().frequencyBasis == "bark24_edges",
             "retain measured frequency provenance");

    pre::ClockSnapshot clock;
    clock.generation = 1;
    clock.positionSamples = 48'000;
    clock.sampleRate = 48'000.0;
    clock.blockFrames = 512;
    clock.playing = true;
    clock.source = pre::ClockSource::projectTimeline;
    auto display = pre::projectDisplay (retained, clock, 1'000, 1'000);
    require (display.status == pre::DisplayStatus::active && display.sectionActive,
             "start boundary is inclusive and activates the PRE section tone");
    const auto measuredBandText = juce::String ("100")
                                + juce::String::charToString (0x2013) + "200 Hz";
    require (display.primary.contains (measuredBandText), "show an exact measured frequency band");
    require (! display.detail.contains (measuredBandText),
             "do not duplicate a masking band across both compact display lines");
    require (display.primary.length() <= 48 && display.detail.length() <= 48,
             "bound both PRE display lines to the available UI width");
    auto overlapping = retained;
    auto secondBand = retained.items.front();
    secondBand.itemId = "mask_2";
    secondBand.lowHz = 1'720.0;
    secondBand.highHz = 2'000.0;
    overlapping.items.push_back (secondBand);
    auto thirdBand = retained.items.front();
    thirdBand.itemId = "mask_3";
    thirdBand.lowHz = 12'000.0;
    thirdBand.highHz = 15'500.0;
    overlapping.items.push_back (thirdBand);
    display = pre::projectDisplay (overlapping, clock, 1'000, 1'000);
    require (display.detail.contains ("+2") && display.detail.length() <= 48,
             "compact simultaneous masking bands without silently hiding their count");
    display = pre::projectDisplay (retained, clock, 1'000, 3'001);
    require (display.status == pre::DisplayStatus::active
             && display.detail.contains ("PAUSED"),
             "retain the active guide while the project clock is paused or stale");
    clock.positionSamples = 96'000;
    display = pre::projectDisplay (retained, clock, 1'000, 1'000);
    require (display.status == pre::DisplayStatus::end && ! display.sectionActive,
             "end boundary is exclusive and restores the PRE context tone");

    const auto pointerFile = root.getChildFile ("active").getChildFile ("kirin_os.json");
    auto pointerWithoutInstant = juce::JSON::parse (pointerFile);
    pointerWithoutInstant.getDynamicObject()->removeProperty ("activated_at");
    require (writeJson (pointerFile, pointerWithoutInstant), "write pointer without instant");
    repository.refresh (retained);
    require (retained.guideId == testUuid ("masking_guide"),
             "pointer without canonical instant retains last valid guide");
    require (pointerFile.replaceWithText ("{broken"), "corrupt pointer fixture");
    const auto corruptPointerReceipt = repository.refresh (retained);
    require (corruptPointerReceipt.state == pre::GuideRefreshState::unavailable
             && retained.guideId == testUuid ("masking_guide"),
             "corrupt pointer retains last valid guide without inventing a rejection identity");

    const auto clearFile = root.getChildFile ("retired").getChildFile ("kirin_os.clear.json");
    require (writeJson (clearFile, clearAuthority ("not_kirin_os")), "write invalid clear");
    repository.refresh (retained);
    require (retained.valid(), "invalid clear cannot remove a guide");
    auto missingInstantClear = clearAuthority ("kirin_os");
    missingInstantClear.getDynamicObject()->removeProperty ("retired_at");
    require (writeJson (clearFile, missingInstantClear), "write clear without instant");
    repository.refresh (retained);
    require (retained.valid(), "clear without canonical instant cannot remove a guide");
    require (writeJson (clearFile, clearAuthority ("kirin_os")), "write valid clear");
    const auto clearReceipt = repository.refresh (retained);
    require (clearReceipt.state == pre::GuideRefreshState::cleared && ! retained.valid(),
             "valid group clear is the sole removal authority");
    require (clearFile.deleteFile(), "remove clear fixture");

    publishGuide (root, measured);
    pre::GuideModel first;
    pre::GuideModel second;
    pre::GuideModel third;
    pre::GuideRepository (root).refresh (first);
    pre::GuideRepository (root).refresh (second);
    pre::GuideRepository (root).refresh (third);
    require (first.valid() && second.valid() && third.valid(),
             "all simultaneous PRE instances receive the same guide");
    require (first.contentHash == second.contentHash && second.contentHash == third.contentHash,
             "all PRE instances retain identical content identity");

    const auto replacement = maskingGuide ("replacement_guide", hashB, true, true, true);
    publishGuide (root, replacement, hashA);
    const auto rejectedReceipt = repository.refresh (first);
    require (rejectedReceipt.state == pre::GuideRefreshState::rejected
             && rejectedReceipt.guideId == testUuid ("replacement_guide")
             && first.guideId == testUuid ("masking_guide"),
             "artifact hash mismatch reports the rejected identity and retains the last valid guide");

    pre::GuideModel legacy;
    auto legacyValue = maskingGuide ("legacy_guide", hashA, false, false, true);
    require (pre::parseArtifactVerifiedGuideModel (
                 *legacyValue.getDynamicObject(), "legacy", legacy),
             "read legacy v1 masking guide");
    require (! legacy.items.front().hasBand,
             "suppress unproven centre-derived legacy frequency edges");
    pre::GuideModel missingBasis;
    auto missingBasisValue = maskingGuide ("invalid_basis", hashA, true, false, true);
    require (! pre::parseArtifactVerifiedGuideModel (
                 *missingBasisValue.getDynamicObject(), "missing", missingBasis),
             "reject new measured band without frequency provenance");
    auto inventedEdgesValue = maskingGuide ("invented_edges", hashA, true, true, true);
    auto* inventedInterval = inventedEdgesValue.getDynamicObject()->getProperty ("payload")
        .getDynamicObject()->getProperty ("intervals").getArray()->getReference (0)
        .getDynamicObject();
    inventedInterval->getProperty ("band").getDynamicObject()->setProperty ("low_hz", 101.0);
    pre::GuideModel inventedEdges;
    require (! pre::parseArtifactVerifiedGuideModel (
                 *inventedEdgesValue.getDynamicObject(), "invented", inventedEdges),
             "reject a band that does not match its declared measured edge table");
    auto threeBandValue = maskingGuide ("three_band", hashA, true, true, true);
    auto* threeBandInterval = threeBandValue.getDynamicObject()->getProperty ("payload")
        .getDynamicObject()->getProperty ("intervals").getArray()->getReference (0)
        .getDynamicObject();
    threeBandInterval->setProperty ("frequency_basis", "three_band_edges");
    threeBandInterval->getProperty ("band").getDynamicObject()->setProperty ("low_hz", 20.0);
    threeBandInterval->getProperty ("band").getDynamicObject()->setProperty ("high_hz", 250.0);
    pre::GuideModel threeBand;
    require (pre::parseArtifactVerifiedGuideModel (
                 *threeBandValue.getDynamicObject(), "three-band", threeBand),
             "accept the native 20–250 Hz sub measurement boundary");
    auto wrongTypeValue = maskingGuide ("wrong_type", hashA, true, true, true);
    wrongTypeValue.getDynamicObject()->setProperty ("guide_id", 123);
    pre::GuideModel wrongType;
    require (! pre::parseArtifactVerifiedGuideModel (
                 *wrongTypeValue.getDynamicObject(), "type", wrongType),
             "reject non-string protocol identifiers instead of coercing them");
    require (! wrongType.valid(), "failed parse does not leak a partial guide model");
    auto unknownFieldValue = maskingGuide ("unknown_field", hashA, true, true, true);
    unknownFieldValue.getDynamicObject()->setProperty ("hidden", true);
    pre::GuideModel unknownField;
    require (! pre::parseArtifactVerifiedGuideModel (
                 *unknownFieldValue.getDynamicObject(), "unknown", unknownField),
             "reject a hash-valid guide with an unknown protocol field");
    auto mutatedHashValue = maskingGuide ("mutated_hash", hashA, true, true, true);
    mutatedHashValue.getDynamicObject()->getProperty ("payload").getDynamicObject()
        ->setProperty ("pair_key", "mutated_pair");
    pre::GuideModel mutatedBody;
    require (pre::parseArtifactVerifiedGuideModel (
                 *mutatedHashValue.getDynamicObject(), "mutated", mutatedBody),
             "treat content_hash as the producer identity after repository artifact verification");

    pre::GuideModel emptyMeasured;
    auto emptyMeasuredValue = maskingGuide ("empty_measured", hashA, true, true, false);
    require (pre::parseArtifactVerifiedGuideModel (
                 *emptyMeasuredValue.getDynamicObject(), "empty", emptyMeasured),
             "accept a measured guide with no masking intervals");
    display = pre::projectDisplay (emptyMeasured, {}, 0, 0);
    require (display.primary == "MASKING  NO TIMED ITEMS",
             "distinguish measured empty result from unavailable measurement");

    pre::GuideModel inspect;
    auto inspectValue = inspectGuide();
    require (pre::parseArtifactVerifiedGuideModel (
                 *inspectValue.getDynamicObject(), "inspect", inspect),
             "retain INSPECT compatibility");
    clock.positionSamples = 48'000;
    display = pre::projectDisplay (inspect, clock, 1'000, 1'000);
    require (display.status == pre::DisplayStatus::active
             && display.sectionActive && display.primary.contains ("True Peak"),
             "project an INSPECT event with an active section tone");
    clock.positionSamples = 120'000;
    display = pre::projectDisplay (inspect, clock, 1'000, 1'000);
    require (display.status == pre::DisplayStatus::active
             && ! display.sectionActive && display.detail.contains ("HELD"),
             "retain INSPECT context after an event without retaining its active section tone");
    auto overlappingInspect = inspect;
    pre::GuideItem heldEvent;
    heldEvent.itemId = "held_event";
    heldEvent.label = "Finished Event";
    heldEvent.sourceLabel = "Source A";
    heldEvent.startNs = 1'200'000'000;
    heldEvent.endNs = 1'400'000'000;
    overlappingInspect.items.push_back (heldEvent);
    overlappingInspect.focusEventId = heldEvent.itemId;
    clock.positionSamples = 72'000;
    display = pre::projectDisplay (overlappingInspect, clock, 1'000, 1'000);
    require (display.sectionActive && display.primary.contains ("True Peak")
             && ! display.primary.contains ("Finished Event"),
             "an active INSPECT fact always outranks a focused held fact");

    pre::ClockReaderState reader;
    reader.accept (clock, 12'345);
    pre::ClockTap emptyTap;
    require (! reader.refresh (emptyTap, 99'999), "detect a transient clock read miss");
    require (reader.snapshot().generation == clock.generation
             && reader.observedAtMs() == 12'345,
             "clock read miss retains the last coherent snapshot and observation time");

    pre::RuntimeIdentity identity;
    identity.instanceId = "pre_instance_1";
    identity.projectUuid = "project_1";
    identity.pluginVersion = "1.0.0";
    identity.pluginFormat = "VST3";
    identity.platform = "test";
    identity.architecture = "test";
    identity.hostProcessId = 42;
    require (pre::writePresence (root, identity, clock, 12'345, 13'000),
             "write presence independently from guide projection");
    require (pre::writeCapability (root, identity, 13'000),
             "advertise receipt capability independently from successful guide parsing");
    auto invalidIdentity = identity;
    invalidIdentity.instanceId = "../escape";
    require (! pre::writePresence (root, invalidIdentity, clock, 12'345, 13'000),
             "reject a lease identity that could escape its transport shelf");
    require (! pre::writeCapability (root, identity, pre::maxSafeJsonInteger),
             "reject a lease whose expiry would exceed the safe JSON integer range");
    const auto presence = juce::JSON::parse (
        root.getChildFile ("presence").getChildFile ("pre_instance_1.json"));
    require (presence.getDynamicObject() != nullptr
             && presence.getDynamicObject()->getProperty ("version") == "1.0"
             && ! presence.getDynamicObject()->getProperty ("capabilities")
                    .getDynamicObject()->hasProperty ("guide_acknowledgement")
             && presence.getDynamicObject()->getProperty ("lease_expires_at_ms")
                    == juce::var (static_cast<juce::int64> (14'500)),
             "presence lease uses the supplied poll time");
    const auto capability = juce::JSON::parse (
        root.getChildFile ("capability").getChildFile ("pre_instance_1.json"));
    require (capability.getDynamicObject() != nullptr
             && capability.getDynamicObject()->getProperty ("version") == "1.0"
             && capability.getDynamicObject()->getProperty ("acknowledgement_protocol") == "1.2"
             && capability.getDynamicObject()->getProperty ("lease_expires_at_ms")
                    == juce::var (static_cast<juce::int64> (14'500)),
             "capability lease identifies the current receipt-capable runtime");
    require (pre::writeAcknowledgement (root, identity, inspect, display, 13'000),
             "write acknowledgement only after strict guide projection");
    const auto acknowledgement = juce::JSON::parse (
        root.getChildFile ("ack").getChildFile ("pre_instance_1.json"));
    require (acknowledgement.getDynamicObject() != nullptr
             && acknowledgement.getDynamicObject()->getProperty ("version") == "1.2"
             && acknowledgement.getDynamicObject()->getProperty ("instance_id") == identity.instanceId
             && acknowledgement.getDynamicObject()->getProperty ("host_process_id")
                    == juce::var (static_cast<juce::int64> (identity.hostProcessId))
             && acknowledgement.getDynamicObject()->getProperty ("project_uuid") == identity.projectUuid
             && acknowledgement.getDynamicObject()->getProperty ("guide_id") == inspect.guideId
             && acknowledgement.getDynamicObject()->getProperty ("content_hash") == inspect.contentHash
             && acknowledgement.getDynamicObject()->getProperty ("receipt_status") == "accepted"
             && acknowledgement.getDynamicObject()->getProperty ("display_status") == "active"
             && acknowledgement.getDynamicObject()->getProperty ("lease_expires_at_ms")
                    == juce::var (static_cast<juce::int64> (14'500)),
             "acknowledgement binds instance, guide, projection state, and lease");
    require (pre::writeRejectedAcknowledgement (root, identity, rejectedReceipt, 13'100),
             "write a bounded rejection receipt for a fully identified active pointer");
    auto invalidRejectedReceipt = rejectedReceipt;
    invalidRejectedReceipt.contentHash = "not-a-hash";
    require (! pre::writeRejectedAcknowledgement (root, identity, invalidRejectedReceipt, 13'100),
             "reject malformed receipt identity before writing the shared transport shelf");
    const auto rejection = juce::JSON::parse (
        root.getChildFile ("ack").getChildFile ("pre_instance_1.json"));
    require (rejection.getDynamicObject() != nullptr
             && rejection.getDynamicObject()->getProperty ("guide_id") == rejectedReceipt.guideId
             && rejection.getDynamicObject()->getProperty ("receipt_status") == "rejected"
             && rejection.getDynamicObject()->getProperty ("display_status").isVoid()
             && rejection.getDynamicObject()->getProperty ("rejection_code")
                    == "guide_contract_rejected",
             "rejection receipt exposes no internal parser detail and never claims a display state");

    for (int iteration = 0; iteration < 32; ++iteration)
    {
        pre::ClockTap lifecycleClock;
        lifecycleClock.publish (iteration, 48'000.0, 512, false,
                                pre::ClockSource::projectTimeline);
        pre::Controller controller (lifecycleClock, root);
        auto lifecycleIdentity = identity;
        lifecycleIdentity.instanceId = "lifecycle_" + juce::String (iteration);
        controller.configureAndStart (std::move (lifecycleIdentity));
    }
    require (true, "repeated controller destruction joins its worker without forced termination");

    const auto activeLifecyclePresence = root.getChildFile ("presence")
        .getChildFile ("lifecycle_active.json");
    const auto activeLifecycleCapability = root.getChildFile ("capability")
        .getChildFile ("lifecycle_active.json");
    const auto activeLifecycleAcknowledgement = root.getChildFile ("ack")
        .getChildFile ("lifecycle_active.json");
    {
        pre::ClockTap lifecycleClock;
        lifecycleClock.publish (0, 48'000.0, 512, false,
                                pre::ClockSource::projectTimeline);
        pre::Controller controller (lifecycleClock, root);
        auto lifecycleIdentity = identity;
        lifecycleIdentity.instanceId = "lifecycle_active";
        controller.configureAndStart (std::move (lifecycleIdentity));
        for (int attempt = 0; attempt < 100
             && (! activeLifecyclePresence.existsAsFile()
                 || ! activeLifecycleCapability.existsAsFile()); ++attempt)
            juce::Thread::sleep (10);
        require (activeLifecyclePresence.existsAsFile(),
                 "run the lifecycle test with an active file-writing worker");
        require (activeLifecycleCapability.existsAsFile(),
                 "active controller publishes its independent capability lease");
    }
    require (! activeLifecyclePresence.existsAsFile(),
             "joined controller removes its own presence after active I/O");
    require (! activeLifecycleCapability.existsAsFile()
             && ! activeLifecycleAcknowledgement.existsAsFile(),
             "joined controller removes its capability and receipt leases after active I/O");

    {
        pre::ClockTap disabledClock;
        pre::Controller disabledController (disabledClock, {});
        disabledController.configureAndStart (identity);
    }
    require (true, "an unresolved transport root disables delivery instead of using a relative path");

    require (root.deleteRecursively(), "remove isolated transport root");
    std::cout << "PRE display runtime contract: PASS\n";
    return 0;
}
