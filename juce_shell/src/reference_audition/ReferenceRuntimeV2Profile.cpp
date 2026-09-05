#include "ReferenceRuntimeV2Profile.h"

#include <cmath>
#include <limits>
#include <regex>
#include <set>
#include <utility>

#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
    namespace
    {
        constexpr std::int64_t maximumProfileBytes = 128 * 1024;
        constexpr std::int64_t maximumSourceProfileBytes = 512 * 1024;

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

        bool exactString (const juce::var& value, juce::String& result)
        {
            if (! value.isString())
                return false;
            result = value.toString();
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

        bool matches (const juce::String& value, const char* expression)
        {
            return std::regex_match (value.toStdString(), std::regex (expression));
        }

        bool uuidV4 (const juce::String& value)
        {
            return matches (value, R"(^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$)");
        }

        bool sha256 (const juce::String& value)
        {
            return matches (value, R"(^[a-f0-9]{64}$)");
        }

        bool displayText (const juce::var& value, juce::String& result)
        {
            if (! exactString (value, result) || result.isEmpty()
                || result.length() > 80 || result.trim() != result)
                return false;
            for (auto character : result)
                if (character < 0x20 || (character >= 0x7f && character <= 0x9f)
                    || character == 0x2028 || character == 0x2029)
                    return false;
            return true;
        }

        bool readJson (const juce::File& file, const RuntimeContentReceipt& receipt,
                       juce::var& value)
        {
            if (! file.existsAsFile() || file.isSymbolicLink()
                || receipt.bytes < 1 || receipt.bytes > maximumProfileBytes
                || file.getSize() != receipt.bytes)
                return false;
            juce::MemoryBlock bytes;
            auto stream = file.createInputStream();
            if (stream == nullptr || ! stream->openedOk())
                return false;
            bytes.setSize (static_cast<size_t> (receipt.bytes), false);
            if (stream->read (bytes.getData(), static_cast<int> (receipt.bytes)) != receipt.bytes
                || juce::SHA256 (bytes).toHexString() != receipt.sha256)
                return false;
            const auto* raw = static_cast<const char*> (bytes.getData());
            if (receipt.bytes >= 3 && static_cast<unsigned char> (raw[0]) == 0xef
                && static_cast<unsigned char> (raw[1]) == 0xbb
                && static_cast<unsigned char> (raw[2]) == 0xbf)
                return false;
            if (! juce::CharPointer_UTF8::isValidString (raw, static_cast<int> (receipt.bytes)))
                return false;
            value = juce::JSON::parse (juce::String::fromUTF8 (raw, static_cast<int> (receipt.bytes)));
            return ! value.isVoid();
        }

        bool parseSourceReceipt (const juce::var& value, RuntimeProfileSourceReceipt& result)
        {
            const auto* object = value.getDynamicObject();
            return object != nullptr
                && exactProperties (*object, { "profile_id", "revision_id", "relative_path", "sha256", "bytes" })
                && exactString (object->getProperty ("profile_id"), result.profileId)
                && exactString (object->getProperty ("revision_id"), result.revisionId)
                && exactString (object->getProperty ("relative_path"), result.relativePath)
                && exactString (object->getProperty ("sha256"), result.sha256)
                && uuidV4 (result.profileId) && uuidV4 (result.revisionId) && sha256 (result.sha256)
                && result.relativePath == "reference/profiles/" + result.profileId + "/"
                                             + result.revisionId + ".v1.json"
                && exactInteger (object->getProperty ("bytes"), 1,
                                 maximumSourceProfileBytes, result.bytes);
        }

        bool parseAxis (const juce::var& value, int minimumItems, int maximumItems,
                        double minimum, double maximum, bool normalized,
                        std::vector<double>& result)
        {
            const auto* array = value.getArray();
            if (array == nullptr || array->size() < minimumItems || array->size() > maximumItems)
                return false;
            double previous = -std::numeric_limits<double>::infinity();
            for (const auto& item : *array)
            {
                if (! item.isDouble() && ! item.isInt() && ! item.isInt64())
                    return false;
                const auto point = static_cast<double> (item);
                if (! std::isfinite (point) || point < minimum || point > maximum || point <= previous)
                    return false;
                if (normalized && std::floor (point) != point)
                    return false;
                result.push_back (point);
                previous = point;
            }
            return ! normalized || (result.front() == 0.0 && result.back() == 10'000.0);
        }

        bool parseQuantile (const juce::var& value, size_t length,
                            std::int64_t minimum, std::int64_t maximum,
                            std::vector<std::optional<std::int64_t>>& result)
        {
            const auto* array = value.getArray();
            if (array == nullptr || array->size() != static_cast<int> (length))
                return false;
            for (const auto& item : *array)
            {
                if (item.isVoid())
                {
                    result.emplace_back();
                    continue;
                }
                std::int64_t parsed = 0;
                if (! exactInteger (item, minimum, maximum, parsed))
                    return false;
                result.emplace_back (parsed);
            }
            return true;
        }

        bool parseDistribution (const juce::var& value, size_t length,
                                std::int64_t sourceCount, std::int64_t minimum,
                                std::int64_t maximum, RuntimeProfileDistribution& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, { "contributor_count", "p10", "median", "p90" }))
                return false;
            const auto* counts = object->getProperty ("contributor_count").getArray();
            if (counts == nullptr || counts->size() != static_cast<int> (length)
                || ! parseQuantile (object->getProperty ("p10"), length, minimum, maximum, result.p10)
                || ! parseQuantile (object->getProperty ("median"), length, minimum, maximum, result.median)
                || ! parseQuantile (object->getProperty ("p90"), length, minimum, maximum, result.p90))
                return false;
            bool hasFacts = false;
            for (size_t index = 0; index < length; ++index)
            {
                std::int64_t count = 0;
                if (! exactInteger ((*counts)[static_cast<int> (index)], 0, sourceCount, count))
                    return false;
                result.contributorCount.push_back (count);
                const bool all = result.p10[index] && result.median[index] && result.p90[index];
                const bool none = ! result.p10[index] && ! result.median[index] && ! result.p90[index];
                if ((count < 3 && ! none) || (count >= 3 && ! all)
                    || (all && (*result.p10[index] > *result.median[index]
                                || *result.median[index] > *result.p90[index])))
                    return false;
                hasFacts = hasFacts || all;
            }
            return hasFacts;
        }

        bool parsePositionView (const juce::var& value, const std::vector<std::string>& series,
                                const std::vector<std::pair<std::int64_t, std::int64_t>>& bounds,
                                std::int64_t sourceCount, RuntimeProfileView& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || object->getProperties().size() != static_cast<int> (series.size() + 1)
                || ! object->hasProperty ("position_basis_points")
                || ! parseAxis (object->getProperty ("position_basis_points"), 2, 256,
                               0.0, 10'000.0, true, result.axis))
                return false;
            for (size_t index = 0; index < series.size(); ++index)
            {
                const juce::Identifier seriesName { juce::String (series[index]) };
                if (! object->hasProperty (seriesName))
                    return false;
                RuntimeProfileDistribution distribution;
                if (! parseDistribution (object->getProperty (seriesName), result.axis.size(),
                                        sourceCount, bounds[index].first, bounds[index].second,
                                        distribution))
                    return false;
                result.series.emplace (series[index], std::move (distribution));
            }
            return true;
        }

        bool parseSpectrum (const juce::var& value, std::int64_t sourceCount,
                            RuntimeProfileView& result)
        {
            const auto* object = value.getDynamicObject();
            RuntimeProfileDistribution distribution;
            return object != nullptr
                && exactProperties (*object, { "band_centers_hz", "level_millidbfs" })
                && parseAxis (object->getProperty ("band_centers_hz"), 12, 128,
                             std::numeric_limits<double>::min(), 384'000.0, false, result.axis)
                && parseDistribution (object->getProperty ("level_millidbfs"), result.axis.size(),
                                     sourceCount, -300'000, 24'000, distribution)
                && result.series.emplace ("level_millidbfs", std::move (distribution)).second;
        }

        bool parseViews (const juce::var& value, RuntimeProfile& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, {
                    "waveform", "spectrum", "loudness", "dynamics", "transient", "stereo" }))
                return false;
            struct Definition
            {
                const char* name;
                std::vector<std::string> series;
                std::vector<std::pair<std::int64_t, std::int64_t>> bounds;
            };
            const std::vector<Definition> definitions {
                { "waveform", { "sample_peak_millidbfs", "rms_millidbfs" },
                    { { -300'000, 24'000 }, { -300'000, 24'000 } } },
                { "loudness", { "lufs_m_millilu", "lufs_s_millilu" },
                    { { -300'000, 100'000 }, { -300'000, 100'000 } } },
                { "dynamics", { "psr_millidb", "crest_millidb" },
                    { { -300'000, 100'000 }, { -300'000, 100'000 } } },
                { "transient", { "onset_strength_q15" }, { { 0, 32'767 } } },
                { "stereo", { "correlation_milli", "width_basis_points" },
                    { { -1'000, 1'000 }, { 0, 15'000 } } },
            };
            for (const auto& definition : definitions)
            {
                const auto item = object->getProperty (definition.name);
                if (item.isVoid())
                    continue;
                RuntimeProfileView view;
                if (! parsePositionView (item, definition.series, definition.bounds,
                                        result.sourceCount, view))
                    return false;
                result.views.emplace (definition.name, std::move (view));
            }
            const auto spectrum = object->getProperty ("spectrum");
            if (! spectrum.isVoid())
            {
                RuntimeProfileView view;
                if (! parseSpectrum (spectrum, result.sourceCount, view))
                    return false;
                result.views.emplace ("spectrum", std::move (view));
            }
            if (result.views.empty())
                return false;
            const auto waveform = result.views.find ("waveform");
            if (waveform != result.views.end())
            {
                const auto& peak = waveform->second.series.at ("sample_peak_millidbfs");
                const auto& rms = waveform->second.series.at ("rms_millidbfs");
                for (size_t index = 0; index < peak.median.size(); ++index)
                    for (const auto pair : { std::pair { &peak.p10, &rms.p10 },
                                             std::pair { &peak.median, &rms.median },
                                             std::pair { &peak.p90, &rms.p90 } })
                        if ((*pair.first)[index] && (*pair.second)[index]
                            && *(*pair.second)[index] > *(*pair.first)[index])
                            return false;
            }
            return true;
        }

        bool parseProfile (const juce::var& value, RuntimeProfile& result)
        {
            const auto* object = value.getDynamicObject();
            return object != nullptr
                && exactProperties (*object, {
                    "format", "version", "source_profile_artifact", "name", "source_count", "views" })
                && object->getProperty ("format") == "kirin_hypha_reference_profile"
                && object->getProperty ("version") == "2.0"
                && parseSourceReceipt (object->getProperty ("source_profile_artifact"),
                                      result.sourceProfileArtifact)
                && displayText (object->getProperty ("name"), result.name)
                && exactInteger (object->getProperty ("source_count"), 3, 64, result.sourceCount)
                && parseViews (object->getProperty ("views"), result);
        }
    }

    RuntimeV2ProfileRepository::RuntimeV2ProfileRepository (juce::File transportRootIn)
        : root (std::move (transportRootIn))
    {
    }

    RuntimeProfileLoadResult RuntimeV2ProfileRepository::load (
        const RuntimeContentReceipt& receipt) const
    {
        if (! sha256 (receipt.sha256)
            || receipt.relativePath != "plugin_data/reference/v2/profiles/"
                                         + receipt.sha256 + ".json")
            return { {}, "reference_profile_receipt_rejected" };
        juce::var json;
        if (! readJson (root.getChildFile ("profiles").getChildFile (receipt.sha256 + ".json"),
                        receipt, json))
            return { {}, "reference_profile_receipt_rejected" };
        auto profile = std::make_shared<RuntimeProfile>();
        if (! parseProfile (json, *profile))
            return { {}, "reference_profile_contract_rejected" };
        return { std::shared_ptr<const RuntimeProfile> (std::move (profile)), {} };
    }
}
