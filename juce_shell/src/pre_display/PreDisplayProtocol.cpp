#include "PreDisplayProtocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace hypha::pre_display
{
    namespace
    {
        bool hasOnlyProperties (const juce::DynamicObject& object,
                                std::initializer_list<const char*> allowed)
        {
            std::set<std::string> names;
            for (const auto* name : allowed)
                names.emplace (name);
            const auto& properties = object.getProperties();
            for (int index = 0; index < properties.size(); ++index)
                if (names.find (properties.getName (index).toString().toStdString()) == names.end())
                    return false;
            return true;
        }

        bool hasProperties (const juce::DynamicObject& object,
                            std::initializer_list<const char*> required)
        {
            for (const auto* name : required)
                if (! object.hasProperty (name))
                    return false;
            return true;
        }

        bool canonicalUuid (const juce::String& value)
        {
            if (value.length() != 36 || value[8] != '-' || value[13] != '-'
                || value[18] != '-' || value[23] != '-')
                return false;
            for (int index = 0; index < value.length(); ++index)
            {
                if (index == 8 || index == 13 || index == 18 || index == 23)
                    continue;
                const auto character = value[index];
                if (! ((character >= '0' && character <= '9')
                       || (character >= 'a' && character <= 'f')
                       || (character >= 'A' && character <= 'F')))
                    return false;
            }
            return true;
        }

        bool safeLowerHex (const juce::String& value, int length)
        {
            if (value.length() != length)
                return false;
            for (const auto character : value)
                if (! (character >= '0' && character <= '9')
                    && ! (character >= 'a' && character <= 'f'))
                    return false;
            return true;
        }

        bool safeDisplayLabel (const juce::String& value)
        {
            if (value.isEmpty() || value.length() > 48 || value != value.trim())
                return false;
            for (const auto character : value)
                if (character < 0x20 || character == 0x7f)
                    return false;
            return true;
        }

        juce::String compactText (const juce::String& text, int maximumCharacters)
        {
            if (text.length() <= maximumCharacters)
                return text;
            return text.substring (0, juce::jmax (1, maximumCharacters - 1)).trimEnd()
                 + juce::String::charToString (0x2026);
        }

        juce::String sourceLabelFor (const std::map<std::string, juce::String>& labels,
                                     const juce::String& sourceRef)
        {
            const auto found = labels.find (sourceRef.toStdString());
            return found != labels.end() ? found->second : sourceRef.substring (0, 48);
        }

        bool parseBand (const juce::var& value, GuideItem& item)
        {
            if (value.isVoid() || value.isUndefined())
                return true;
            const auto* band = value.getDynamicObject();
            if (band == nullptr)
                return value.isVoid();
            if (! hasOnlyProperties (*band, { "low_hz", "high_hz" })
                || ! hasProperties (*band, { "low_hz", "high_hz" }))
                return false;
            const auto lowValue = band->getProperty ("low_hz");
            const auto highValue = band->getProperty ("high_hz");
            const auto isNumber = [] (const juce::var& number)
            {
                return number.isDouble() || number.isInt() || number.isInt64();
            };
            if (! isNumber (lowValue) || ! isNumber (highValue))
                return false;
            const auto low = static_cast<double> (lowValue);
            const auto high = static_cast<double> (highValue);
            if (! std::isfinite (low) || ! std::isfinite (high) || low < 0.0 || high <= low
                || high > 1'000'000.0)
                return false;
            item.lowHz = low;
            item.highHz = high;
            item.hasBand = true;
            return true;
        }

        bool validFrequencyBasis (const juce::String& value)
        {
            return value == "bark24_edges" || value == "three_band_edges";
        }

        bool bandMatchesFrequencyBasis (const GuideItem& item)
        {
            static constexpr std::array<double, 25> barkEdges {
                20.0, 100.0, 200.0, 300.0, 400.0, 510.0, 630.0, 770.0, 920.0,
                1'080.0, 1'270.0, 1'480.0, 1'720.0, 2'000.0, 2'320.0,
                2'700.0, 3'150.0, 3'700.0, 4'400.0, 5'300.0, 6'400.0,
                7'700.0, 9'500.0, 12'000.0, 15'500.0,
            };
            if (item.frequencyBasis == "bark24_edges")
            {
                const auto low = std::find (barkEdges.begin(), barkEdges.end(), item.lowHz);
                const auto high = std::find (barkEdges.begin(), barkEdges.end(), item.highHz);
                return low != barkEdges.end() && high != barkEdges.end() && low < high;
            }
            if (item.frequencyBasis == "three_band_edges")
                return (item.lowHz == 20.0 && item.highHz == 250.0)
                    || (item.lowHz == 250.0 && item.highHz == 2'000.0)
                    || (item.lowHz == 2'000.0 && item.highHz == 8'000.0);
            return false;
        }

        bool parseGuideItem (const juce::DynamicObject& object, const GuideModel& model,
                             const std::map<std::string, juce::String>& sourceLabels,
                             const std::map<std::string, std::int64_t>& sourceDurations,
                             GuideItem& item)
        {
            if (model.payloadKind == "inspect")
            {
                if (! hasOnlyProperties (object, { "item_id", "type_id", "display_label",
                                                   "source_ref", "channel_key", "start_ns",
                                                   "end_ns", "band" })
                    || ! hasProperties (object, { "item_id", "type_id", "source_ref",
                                                  "start_ns", "end_ns" }))
                    return false;
            }
            else if (! hasOnlyProperties (object, { "item_id", "selection_ref", "start_ns", "end_ns",
                                                     "frequency_state", "frequency_basis", "band" })
                     || ! hasProperties (object, { "item_id", "start_ns", "end_ns",
                                                   "frequency_state" }))
                return false;
            item.itemId = objectString (object, "item_id");
            if (! safeId (item.itemId)
                || ! objectInteger (object, "start_ns", 0, maxSafeJsonInteger, item.startNs)
                || ! objectInteger (object, "end_ns", 0, maxSafeJsonInteger, item.endNs)
                || item.endNs <= item.startNs
                || ! parseBand (object.getProperty ("band"), item))
                return false;

            if (model.payloadKind == "inspect")
            {
                // Guide v1 represents an event without a measured end as a one-nanosecond
                // half-open wire interval. Keep that fact distinct from the presentation cue.
                item.temporalKind = item.endNs - item.startNs == 1
                    ? TemporalFactKind::instantMarker
                    : TemporalFactKind::measuredInterval;
                const auto typeId = objectString (object, "type_id");
                const auto sourceRef = objectString (object, "source_ref");
                if (! safeId (typeId) || ! safeId (sourceRef))
                    return false;
                const auto displayLabel = objectString (object, "display_label");
                if (object.hasProperty ("display_label")
                    && (! object.getProperty ("display_label").isString()
                        || ! safeDisplayLabel (displayLabel)))
                    return false;
                item.label = displayLabel.isEmpty() ? typeId.substring (0, 48) : displayLabel;
                item.sourceLabel = sourceLabelFor (sourceLabels, sourceRef);
                item.channel = objectString (object, "channel_key");
                const auto duration = sourceDurations.find (sourceRef.toStdString());
                const auto channelValue = object.getProperty ("channel_key");
                const bool channelValid = ! object.hasProperty ("channel_key")
                    || channelValue.isVoid() || channelValue.isUndefined()
                    || (channelValue.isString() && safeId (item.channel));
                return safeDisplayLabel (item.label) && channelValid
                    && duration != sourceDurations.end() && item.endNs <= duration->second
                    && (item.channel.isEmpty() || safeId (item.channel));
            }

            const auto state = objectString (object, "frequency_state");
            item.selectionRef = objectString (object, "selection_ref");
            const bool hasReviewSelection = model.protocolVersion == "1.1"
                                         || model.protocolVersion == "2.0"
                                         || model.protocolVersion == "3.0";
            if (hasReviewSelection && ! safeId (item.selectionRef))
                return false;
            if (! hasReviewSelection && object.hasProperty ("selection_ref"))
                return false;
            if (state != "measured" && state != "unlocated"
                && state != "missing" && state != "invalid")
                return false;
            item.frequencyBasis = objectString (object, "frequency_basis");
            if (object.hasProperty ("frequency_basis")
                && (! object.getProperty ("frequency_basis").isString()
                    || item.frequencyBasis.isEmpty()))
                return false;
            if (state != "measured")
                return ! item.hasBand && item.frequencyBasis.isEmpty();
            if (! item.hasBand)
                return false;
            if (item.frequencyBasis.isNotEmpty())
                return validFrequencyBasis (item.frequencyBasis)
                    && bandMatchesFrequencyBasis (item);
            if (model.maskingMeasurementState.isNotEmpty())
                return false;
            // W-2370 legacy guides did not carry frequency provenance and used
            // centre-derived edges. Retain their timing but suppress the band.
            item.hasBand = false;
            return true;
        }
    }

    bool safeId (const juce::String& value)
    {
        if (value.isEmpty() || value.length() > 128)
            return false;
        for (const auto character : value)
            if (! (character >= 'A' && character <= 'Z')
                && ! (character >= 'a' && character <= 'z')
                && ! (character >= '0' && character <= '9')
                && character != '.' && character != '_' && character != ':' && character != '-')
                return false;
        return true;
    }

    bool safeHash (const juce::String& value)
    {
        if (value.length() != 64)
            return false;
        for (const auto character : value)
            if (! (character >= '0' && character <= '9')
                && ! (character >= 'a' && character <= 'f'))
                return false;
        return true;
    }

    bool safeGuideFileName (const juce::String& value)
    {
        return value.isNotEmpty()
            && value == juce::File::createLegalFileName (value)
            && value.endsWith (".json")
            && ! value.contains ("..")
            && ! value.containsAnyOf ("/\\");
    }

    juce::String objectString (const juce::DynamicObject& object, const char* name)
    {
        const auto value = object.getProperty (name);
        return value.isString() ? value.toString() : juce::String();
    }

    bool objectInteger (const juce::DynamicObject& object, const char* name,
                        std::int64_t minimum, std::int64_t maximum, std::int64_t& result)
    {
        const auto value = object.getProperty (name);
        if (! value.isInt() && ! value.isInt64())
            return false;
        result = static_cast<std::int64_t> (static_cast<juce::int64> (value));
        return result >= minimum && result <= maximum;
    }

    bool parseArtifactVerifiedGuideModel (const juce::DynamicObject& guide,
                                          const juce::String& cacheKey,
                                          GuideModel& model)
    {
        GuideModel candidate;
        candidate.protocolVersion = objectString (guide, "version");
        if (! hasOnlyProperties (guide, { "format", "version", "guide_id", "revision",
                                          "created_at", "content_hash", "producer", "target",
                                          "source_set", "time_basis", "display_locale", "payload" })
            || ! hasProperties (guide, { "format", "version", "guide_id", "revision",
                                         "created_at", "content_hash", "producer", "target",
                                         "source_set", "time_basis", "payload" })
            || objectString (guide, "format") != "kirin_pre_display_guide"
            || (candidate.protocolVersion != "1.0"
                && candidate.protocolVersion != "1.1"
                && candidate.protocolVersion != "2.0"
                && candidate.protocolVersion != "3.0")
            || ! canonicalIsoInstant (objectString (guide, "created_at")))
            return false;
        candidate.cacheKey = cacheKey;
        candidate.guideId = objectString (guide, "guide_id");
        candidate.contentHash = objectString (guide, "content_hash");
        const auto* target = guide.getProperty ("target").getDynamicObject();
        const auto* timeBasis = guide.getProperty ("time_basis").getDynamicObject();
        const auto* payload = guide.getProperty ("payload").getDynamicObject();
        const auto* sourceSet = guide.getProperty ("source_set").getArray();
        const auto* producer = guide.getProperty ("producer").getDynamicObject();
        const auto displayLocale = guide.getProperty ("display_locale");
        if (! canonicalUuid (candidate.guideId) || ! safeHash (candidate.contentHash)
            || target == nullptr || timeBasis == nullptr || payload == nullptr
            || producer == nullptr || sourceSet == nullptr || sourceSet->isEmpty() || sourceSet->size() > 32
            || ! hasOnlyProperties (*producer, { "session_id", "work_id", "report_id", "source_sha256_file" })
            || ! hasProperties (*producer, { "session_id" })
            || ! canonicalUuid (objectString (*producer, "session_id"))
            || (guide.hasProperty ("display_locale")
                && (! displayLocale.isString()
                    || (displayLocale.toString() != "ja" && displayLocale.toString() != "en"))))
            return false;
        for (const auto* optionalId : { "work_id", "report_id" })
        {
            const auto value = producer->getProperty (optionalId);
            if (! value.isVoid() && ! value.isUndefined()
                && (! value.isString() || ! canonicalUuid (value.toString())))
                return false;
        }
        const auto producerWorkId = objectString (*producer, "work_id");
        const auto producerSourceHash = objectString (*producer, "source_sha256_file");
        const auto roleNeutralTarget = candidate.protocolVersion == "3.0";
        const auto exactTarget = candidate.protocolVersion == "2.0" || roleNeutralTarget;
        if (exactTarget
            && (! canonicalUuid (producerWorkId) || ! safeHash (producerSourceHash)))
            return false;
        candidate.groupId = objectString (*target, "group_id");
        candidate.payloadKind = objectString (*payload, "kind");
        const auto reviewTarget = candidate.protocolVersion == "1.1" || exactTarget;
        const std::initializer_list<const char*> legacyTargetFields {
            "group_id", "selection_mode"
        };
        const std::initializer_list<const char*> exactPreTargetFields {
            "group_id", "selection_mode", "work_id", "binding_id", "runtime_instance_id"
        };
        const std::initializer_list<const char*> exactHyphaTargetFields {
            "group_id", "selection_mode", "target_role", "work_id", "binding_id",
            "runtime_instance_id"
        };
        const auto targetFields = roleNeutralTarget ? exactHyphaTargetFields
                                : exactTarget ? exactPreTargetFields : legacyTargetFields;
        if (! hasOnlyProperties (*target, targetFields)
            || ! hasProperties (*target, targetFields)
            || ! hasOnlyProperties (*timeBasis, { "kind", "unit", "interval_convention",
                                                   "source_zero_project_ns", "alignment_method" })
            || ! hasProperties (*timeBasis, { "kind", "unit", "interval_convention",
                                              "source_zero_project_ns", "alignment_method" })
            || ! safeId (candidate.groupId)
            || objectString (*target, "selection_mode") != (roleNeutralTarget
                    ? "exact_hypha_binding" : exactTarget
                        ? "exact_pre_binding" : "all_pre_instances")
            || (roleNeutralTarget && objectString (*target, "target_role") != "post")
            || (candidate.payloadKind != "masking" && candidate.payloadKind != "inspect")
            || ! objectInteger (guide, "revision", 1, maxSafeJsonInteger, candidate.revision)
            || objectString (*timeBasis, "kind") != "source_to_project_offset"
            || objectString (*timeBasis, "unit") != "nanoseconds"
            || objectString (*timeBasis, "interval_convention") != "half_open"
            || (objectString (*timeBasis, "alignment_method") != "project_zero"
                && objectString (*timeBasis, "alignment_method") != "captured_playhead"
                && objectString (*timeBasis, "alignment_method") != "audio_landmark")
            || ! objectInteger (*timeBasis, "source_zero_project_ns", -maxSafeJsonInteger,
                                maxSafeJsonInteger, candidate.sourceZeroProjectNs))
            return false;
        if (exactTarget)
        {
            candidate.targetRole = roleNeutralTarget
                ? GuideTargetRole::post : GuideTargetRole::pre;
            candidate.workId = objectString (*target, "work_id");
            candidate.bindingId = objectString (*target, "binding_id");
            candidate.runtimeInstanceId = objectString (*target, "runtime_instance_id");
            if (! canonicalUuid (candidate.workId) || candidate.workId != producerWorkId
                || ! canonicalUuid (candidate.bindingId) || ! safeId (candidate.runtimeInstanceId))
                return false;
        }

        std::map<std::string, juce::String> sourceLabels;
        std::map<std::string, std::int64_t> sourceDurations;
        for (const auto& sourceValue : *sourceSet)
        {
            const auto* source = sourceValue.getDynamicObject();
            if (source == nullptr)
                return false;
            if (! hasOnlyProperties (*source, { "source_ref", "display_label", "sha256_pcm",
                                                "duration_ns", "sample_rate", "alignment_landmarks" })
                || ! hasProperties (*source, { "source_ref", "duration_ns" }))
                return false;
            const auto ref = objectString (*source, "source_ref");
            auto label = objectString (*source, "display_label");
            std::int64_t duration = 0;
            if (! safeId (ref)
                || (source->hasProperty ("display_label")
                    && (! source->getProperty ("display_label").isString()
                        || ! safeDisplayLabel (label)))
                || ! objectInteger (*source, "duration_ns", 0, maxSafeJsonInteger, duration))
                return false;
            const auto pcmHash = source->getProperty ("sha256_pcm");
            if (source->hasProperty ("sha256_pcm")
                && (! pcmHash.isString() || ! safeHash (pcmHash.toString())))
                return false;
            const auto sampleRate = source->getProperty ("sample_rate");
            if (source->hasProperty ("sample_rate"))
            {
                std::int64_t parsedSampleRate = 0;
                if (! objectInteger (*source, "sample_rate", 8'000, 768'000, parsedSampleRate))
                    return false;
            }
            const auto landmarksValue = source->getProperty ("alignment_landmarks");
            if (source->hasProperty ("alignment_landmarks"))
            {
                const auto* landmarks = landmarksValue.getArray();
                if (landmarks == nullptr || landmarks->size() > 4'096)
                    return false;
                for (const auto& landmarkValue : *landmarks)
                {
                    const auto* landmark = landmarkValue.getDynamicObject();
                    std::int64_t sourceSample = 0;
                    const auto landmarkHash = landmark != nullptr
                        ? objectString (*landmark, "hash") : juce::String();
                    if (landmark == nullptr
                        || ! hasOnlyProperties (*landmark, { "source_sample", "hash" })
                        || ! hasProperties (*landmark, { "source_sample", "hash" })
                        || ! objectInteger (*landmark, "source_sample", 0,
                                            maxSafeJsonInteger, sourceSample)
                        || ! safeLowerHex (landmarkHash, 16))
                        return false;
                }
            }
            if (label.isEmpty())
                label = ref.substring (0, 48);
            if (! sourceLabels.emplace (ref.toStdString(), label).second
                || ! sourceDurations.emplace (ref.toStdString(), duration).second)
                return false;
        }

        const juce::Array<juce::var>* items = nullptr;
        std::map<std::string, GuideReviewSelection> reviewSelections;
        std::int64_t maskingDuration = maxSafeJsonInteger;
        if (candidate.payloadKind == "masking")
        {
            if (! hasOnlyProperties (*payload, reviewTarget
                    ? std::initializer_list<const char*> { "kind", "measurement_state", "pair_key", "source_order", "review_selections", "intervals" }
                    : std::initializer_list<const char*> { "kind", "measurement_state", "pair_key", "source_order", "intervals" })
                || ! hasProperties (*payload, { "kind", "pair_key", "source_order", "intervals" })
                || ! safeId (objectString (*payload, "pair_key")))
                return false;
            candidate.maskingMeasurementState = objectString (*payload, "measurement_state");
            if (payload->hasProperty ("measurement_state")
                && (! payload->getProperty ("measurement_state").isString()
                    || (candidate.maskingMeasurementState != "measured"
                        && candidate.maskingMeasurementState != "legacy_non_directional")))
                return false;
            items = payload->getProperty ("intervals").getArray();
            const auto* order = payload->getProperty ("source_order").getArray();
            if (order == nullptr || order->size() != 2)
                return false;
            const auto aValue = order->getUnchecked (0);
            const auto bValue = order->getUnchecked (1);
            if (! aValue.isString() || ! bValue.isString())
                return false;
            const auto a = aValue.toString();
            const auto b = bValue.toString();
            if (! safeId (a) || ! safeId (b) || a == b
                || sourceLabels.find (a.toStdString()) == sourceLabels.end()
                || sourceLabels.find (b.toStdString()) == sourceLabels.end())
                return false;
            maskingDuration = juce::jmin (sourceDurations.at (a.toStdString()),
                                          sourceDurations.at (b.toStdString()));
            candidate.sourcePairLabel = compactText (sourceLabelFor (sourceLabels, a), 16)
                                      + " " + juce::String::charToString (0x00d7) + " "
                                      + compactText (sourceLabelFor (sourceLabels, b), 16);
            if (reviewTarget)
            {
                const auto* selections = payload->getProperty ("review_selections").getArray();
                if (selections == nullptr || selections->isEmpty() || selections->size() > maxGuideItems)
                    return false;
                for (const auto& selectionValue : *selections)
                {
                    const auto* selection = selectionValue.getDynamicObject();
                    std::int64_t selectionStart = 0, selectionEnd = 0;
                    GuideItem focusBand;
                    if (selection == nullptr
                        || ! hasOnlyProperties (*selection, { "selection_id", "start_ns", "end_ns", "focus_band" })
                        || ! hasProperties (*selection, { "selection_id", "start_ns", "end_ns", "focus_band" })
                        || ! safeId (objectString (*selection, "selection_id"))
                        || ! objectInteger (*selection, "start_ns", 0, maxSafeJsonInteger, selectionStart)
                        || ! objectInteger (*selection, "end_ns", 0, maxSafeJsonInteger, selectionEnd)
                        || selectionEnd <= selectionStart || selectionEnd > maskingDuration
                        || ! parseBand (selection->getProperty ("focus_band"), focusBand)
                        || ! focusBand.hasBand)
                        return false;
                    GuideReviewSelection parsedSelection;
                    parsedSelection.selectionId = objectString (*selection, "selection_id");
                    parsedSelection.startNs = selectionStart;
                    parsedSelection.endNs = selectionEnd;
                    parsedSelection.lowHz = focusBand.lowHz;
                    parsedSelection.highHz = focusBand.highHz;
                    parsedSelection.hasBand = true;
                    if (! reviewSelections.emplace (
                            objectString (*selection, "selection_id").toStdString(),
                            parsedSelection).second)
                        return false;
                }
            }
        }
        else
        {
            if (! hasOnlyProperties (*payload, { "kind", "source_ref", "focus_event_id", "events" })
                || ! hasProperties (*payload, { "kind", "source_ref", "events" }))
                return false;
            items = payload->getProperty ("events").getArray();
            const auto sourceRef = objectString (*payload, "source_ref");
            if (! safeId (sourceRef)
                || sourceLabels.find (sourceRef.toStdString()) == sourceLabels.end())
                return false;
            candidate.focusEventId = objectString (*payload, "focus_event_id");
            const auto focusValue = payload->getProperty ("focus_event_id");
            if (payload->hasProperty ("focus_event_id")
                && ! focusValue.isVoid() && ! focusValue.isUndefined()
                && (! focusValue.isString() || ! safeId (candidate.focusEventId)))
                return false;
        }
        if (items == nullptr || items->size() > maxGuideItems)
            return false;
        std::map<std::string, bool> itemIds;
        candidate.items.reserve (static_cast<std::size_t> (items->size()));
        for (const auto& value : *items)
        {
            const auto* object = value.getDynamicObject();
            GuideItem item;
            if (object == nullptr
                || ! parseGuideItem (*object, candidate, sourceLabels, sourceDurations, item)
                || ! itemIds.emplace (item.itemId.toStdString(), true).second
                || (candidate.payloadKind == "masking" && item.endNs > maskingDuration))
                return false;
            candidate.items.push_back (std::move (item));
        }
        if (candidate.payloadKind == "masking" && reviewTarget)
        {
            for (const auto& item : candidate.items)
            {
                const auto selection = reviewSelections.find (item.selectionRef.toStdString());
                if (selection == reviewSelections.end()
                    || item.startNs < selection->second.startNs
                    || item.endNs > selection->second.endNs
                    || (item.hasBand
                        && ! (item.highHz > selection->second.lowHz
                              && item.lowHz < selection->second.highHz)))
                    return false;
            }
            candidate.reviewSelections.reserve (reviewSelections.size());
            for (const auto& [selectionId, selection] : reviewSelections)
            {
                juce::ignoreUnused (selectionId);
                candidate.reviewSelections.push_back (selection);
            }
            std::stable_sort (candidate.reviewSelections.begin(),
                              candidate.reviewSelections.end(), [] (const auto& a, const auto& b)
            {
                if (a.startNs != b.startNs) return a.startNs < b.startNs;
                return a.selectionId < b.selectionId;
            });
        }
        if (candidate.payloadKind == "inspect" && candidate.focusEventId.isNotEmpty()
            && itemIds.find (candidate.focusEventId.toStdString()) == itemIds.end())
            return false;
        std::stable_sort (candidate.items.begin(), candidate.items.end(), [] (const auto& a, const auto& b)
        {
            if (a.startNs != b.startNs) return a.startNs < b.startNs;
            return a.itemId < b.itemId;
        });
        model = std::move (candidate);
        return true;
    }
}
