#include "PreDisplayProtocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <utility>

namespace hypha::pre_display
{
    namespace
    {
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
            item.itemId = objectString (object, "item_id");
            if (! safeId (item.itemId)
                || ! objectInteger (object, "start_ns", 0, maxSafeJsonInteger, item.startNs)
                || ! objectInteger (object, "end_ns", 0, maxSafeJsonInteger, item.endNs)
                || item.endNs <= item.startNs
                || ! parseBand (object.getProperty ("band"), item))
                return false;

            if (model.payloadKind == "inspect")
            {
                const auto typeId = objectString (object, "type_id");
                const auto sourceRef = objectString (object, "source_ref");
                if (! safeId (typeId) || ! safeId (sourceRef))
                    return false;
                const auto displayLabel = objectString (object, "display_label");
                item.label = displayLabel.isEmpty() ? typeId.substring (0, 48) : displayLabel;
                item.sourceLabel = sourceLabelFor (sourceLabels, sourceRef);
                item.channel = objectString (object, "channel_key");
                const auto duration = sourceDurations.find (sourceRef.toStdString());
                return safeDisplayLabel (item.label)
                    && duration != sourceDurations.end() && item.endNs <= duration->second
                    && (item.channel.isEmpty() || safeId (item.channel));
            }

            const auto state = objectString (object, "frequency_state");
            if (state != "measured" && state != "unlocated"
                && state != "missing" && state != "invalid")
                return false;
            item.frequencyBasis = objectString (object, "frequency_basis");
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

    bool canonicalIsoInstant (const juce::String& value)
    {
        if (value.length() != 24 || value[4] != '-' || value[7] != '-'
            || value[10] != 'T' || value[13] != ':' || value[16] != ':'
            || value[19] != '.' || value[23] != 'Z')
            return false;
        for (int index = 0; index < value.length(); ++index)
        {
            if (index == 4 || index == 7 || index == 10 || index == 13
                || index == 16 || index == 19 || index == 23)
                continue;
            if (! juce::CharacterFunctions::isDigit (value[index]))
                return false;
        }
        const auto year = value.substring (0, 4).getIntValue();
        const auto month = value.substring (5, 7).getIntValue();
        const auto day = value.substring (8, 10).getIntValue();
        const auto hour = value.substring (11, 13).getIntValue();
        const auto minute = value.substring (14, 16).getIntValue();
        const auto second = value.substring (17, 19).getIntValue();
        const auto millisecond = value.substring (20, 23).getIntValue();
        if (year < 1 || month < 1 || month > 12 || hour > 23
            || minute > 59 || second > 59 || millisecond > 999)
            return false;
        static constexpr std::array<int, 12> daysPerMonth {
            31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
        };
        auto maximumDay = daysPerMonth[static_cast<std::size_t> (month - 1)];
        const bool leapYear = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        if (month == 2 && leapYear)
            ++maximumDay;
        return day >= 1 && day <= maximumDay;
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

    bool parseGuideModel (const juce::DynamicObject& guide, const juce::String& cacheKey,
                          GuideModel& model)
    {
        GuideModel candidate;
        if (objectString (guide, "format") != "kirin_pre_display_guide"
            || objectString (guide, "version") != "1.0")
            return false;
        candidate.cacheKey = cacheKey;
        candidate.guideId = objectString (guide, "guide_id");
        candidate.contentHash = objectString (guide, "content_hash");
        const auto* target = guide.getProperty ("target").getDynamicObject();
        const auto* timeBasis = guide.getProperty ("time_basis").getDynamicObject();
        const auto* payload = guide.getProperty ("payload").getDynamicObject();
        const auto* sourceSet = guide.getProperty ("source_set").getArray();
        if (! safeId (candidate.guideId) || ! safeHash (candidate.contentHash)
            || target == nullptr || timeBasis == nullptr || payload == nullptr
            || sourceSet == nullptr || sourceSet->isEmpty() || sourceSet->size() > 32)
            return false;
        candidate.groupId = objectString (*target, "group_id");
        candidate.payloadKind = objectString (*payload, "kind");
        if (! safeId (candidate.groupId)
            || objectString (*target, "selection_mode") != "all_pre_instances"
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

        std::map<std::string, juce::String> sourceLabels;
        std::map<std::string, std::int64_t> sourceDurations;
        for (const auto& sourceValue : *sourceSet)
        {
            const auto* source = sourceValue.getDynamicObject();
            if (source == nullptr)
                return false;
            const auto ref = objectString (*source, "source_ref");
            auto label = objectString (*source, "display_label");
            std::int64_t duration = 0;
            if (! safeId (ref) || (! label.isEmpty() && ! safeDisplayLabel (label))
                || ! objectInteger (*source, "duration_ns", 0, maxSafeJsonInteger, duration))
                return false;
            if (label.isEmpty())
                label = ref.substring (0, 48);
            if (! sourceLabels.emplace (ref.toStdString(), label).second
                || ! sourceDurations.emplace (ref.toStdString(), duration).second)
                return false;
        }

        const juce::Array<juce::var>* items = nullptr;
        std::int64_t maskingDuration = maxSafeJsonInteger;
        if (candidate.payloadKind == "masking")
        {
            candidate.maskingMeasurementState = objectString (*payload, "measurement_state");
            if (candidate.maskingMeasurementState.isNotEmpty()
                && candidate.maskingMeasurementState != "measured"
                && candidate.maskingMeasurementState != "legacy_non_directional")
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
            candidate.sourcePairLabel = compactText (sourceLabelFor (sourceLabels, a), 16) + " × "
                                      + compactText (sourceLabelFor (sourceLabels, b), 16);
        }
        else
        {
            items = payload->getProperty ("events").getArray();
            const auto sourceRef = objectString (*payload, "source_ref");
            if (! safeId (sourceRef)
                || sourceLabels.find (sourceRef.toStdString()) == sourceLabels.end())
                return false;
            candidate.focusEventId = objectString (*payload, "focus_event_id");
            if (! candidate.focusEventId.isEmpty() && ! safeId (candidate.focusEventId))
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
