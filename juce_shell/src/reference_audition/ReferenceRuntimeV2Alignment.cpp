#include "ReferenceRuntimeV2Alignment.h"

#include <regex>
#include <utility>

#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
    namespace
    {
        constexpr std::int64_t maximumBytes = 512 * 1024;
        constexpr std::int64_t maximumSafeInteger = 9'007'199'254'740'991;

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

        bool sha256 (const juce::String& value)
        {
            return std::regex_match (value.toStdString(), std::regex (R"(^[a-f0-9]{64}$)"));
        }

        bool readJson (const juce::File& file, const RuntimeContentReceipt& receipt,
                       juce::var& value)
        {
            if (! file.existsAsFile() || file.isSymbolicLink()
                || receipt.bytes < 1 || receipt.bytes > maximumBytes
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

        bool parseAudio (const juce::var& value, RuntimeAudioFacts& result)
        {
            const auto* object = value.getDynamicObject();
            return object != nullptr
                && exactProperties (*object, { "sample_rate_hz", "channels", "total_sample_frames" })
                && exactInteger (object->getProperty ("sample_rate_hz"), 8'000, 768'000,
                                 result.sampleRateHz)
                && exactInteger (object->getProperty ("channels"), 1, 2, result.channels)
                && exactInteger (object->getProperty ("total_sample_frames"), 1,
                                 maximumSafeInteger, result.totalSampleFrames);
        }

        bool parseRequiredSeries (const juce::var& value, size_t count,
                                  std::int64_t minimum, std::int64_t maximum,
                                  std::vector<std::int64_t>& result)
        {
            const auto* array = value.getArray();
            if (array == nullptr || array->size() != static_cast<int> (count))
                return false;
            for (const auto& item : *array)
            {
                std::int64_t parsed = 0;
                if (! exactInteger (item, minimum, maximum, parsed))
                    return false;
                result.push_back (parsed);
            }
            return true;
        }

        bool parseNullableSeries (const juce::var& value, size_t count,
                                  std::int64_t minimum, std::int64_t maximum,
                                  RuntimeAlignmentNullableSeries& result)
        {
            const auto* array = value.getArray();
            if (array == nullptr || array->size() != static_cast<int> (count))
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

        bool parseGrid (const juce::var& value, const RuntimeAudioFacts& audio,
                        RuntimeAlignmentGrid& result)
        {
            const auto* object = value.getDynamicObject();
            std::int64_t start = -1;
            return object != nullptr
                && exactProperties (*object, { "start_sample", "hop_samples", "point_count" })
                && exactInteger (object->getProperty ("start_sample"), 0, 0, start)
                && exactInteger (object->getProperty ("hop_samples"), 1,
                                 maximumSafeInteger, result.hopSamples)
                && exactInteger (object->getProperty ("point_count"), 2, 2048,
                                 result.pointCount)
                && (audio.totalSampleFrames + result.hopSamples - 1) / result.hopSamples
                     == result.pointCount;
        }

        bool parseFeatures (const juce::var& value, RuntimeAlignment& result)
        {
            const auto* object = value.getDynamicObject();
            const auto count = static_cast<size_t> (result.grid.pointCount);
            const auto* chroma = object == nullptr ? nullptr
                : object->getProperty ("chroma_q15").getArray();
            if (object == nullptr || ! exactProperties (*object, {
                    "onset_strength_q15", "sub_energy_millidbfs", "bass_energy_millidbfs",
                    "mid_energy_millidbfs", "high_energy_millidbfs", "chroma_q15",
                    "loudness_millilu" })
                || chroma == nullptr || chroma->size() != 12
                || ! parseRequiredSeries (object->getProperty ("onset_strength_q15"), count,
                                         0, 32'767, result.features.onsetStrengthQ15)
                || ! parseNullableSeries (object->getProperty ("sub_energy_millidbfs"), count,
                                         -300'000, 24'000, result.features.subEnergyMillidbfs)
                || ! parseNullableSeries (object->getProperty ("bass_energy_millidbfs"), count,
                                         -300'000, 24'000, result.features.bassEnergyMillidbfs)
                || ! parseNullableSeries (object->getProperty ("mid_energy_millidbfs"), count,
                                         -300'000, 24'000, result.features.midEnergyMillidbfs)
                || ! parseNullableSeries (object->getProperty ("high_energy_millidbfs"), count,
                                         -300'000, 24'000, result.features.highEnergyMillidbfs)
                || ! parseNullableSeries (object->getProperty ("loudness_millilu"), count,
                                         -300'000, 100'000, result.features.loudnessMillilu))
                return false;
            for (int channel = 0; channel < 12; ++channel)
                if (! parseNullableSeries ((*chroma)[channel], count, 0, 32'767,
                                          result.features.chromaQ15[static_cast<size_t> (channel)]))
                    return false;

            int contentPoints = 0;
            for (size_t index = 0; index < count; ++index)
            {
                bool hasContent = result.features.onsetStrengthQ15[index] > 0
                    || (result.features.subEnergyMillidbfs[index]
                        && *result.features.subEnergyMillidbfs[index] > -300'000)
                    || (result.features.bassEnergyMillidbfs[index]
                        && *result.features.bassEnergyMillidbfs[index] > -300'000)
                    || (result.features.midEnergyMillidbfs[index]
                        && *result.features.midEnergyMillidbfs[index] > -300'000)
                    || (result.features.highEnergyMillidbfs[index]
                        && *result.features.highEnergyMillidbfs[index] > -300'000)
                    || result.features.loudnessMillilu[index].has_value();
                for (const auto& series : result.features.chromaQ15)
                    hasContent = hasContent || series[index].has_value();
                if (hasContent)
                    ++contentPoints;
            }
            return contentPoints >= 2;
        }

        bool parseAlignment (const juce::var& value, const RuntimeSource& source,
                             RuntimeAlignment& result)
        {
            const auto* object = value.getDynamicObject();
            const auto* content = object == nullptr ? nullptr
                : object->getProperty ("source_content").getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, {
                    "format", "version", "feature_profile", "source_content", "audio",
                    "grid", "features" })
                || object->getProperty ("format") != "kirin_hypha_reference_alignment"
                || object->getProperty ("version") != "2.0"
                || object->getProperty ("feature_profile") != "kirin_content_features_v1"
                || content == nullptr || ! exactProperties (*content, { "sha256_file", "sha256_pcm" })
                || ! content->getProperty ("sha256_file").isString()
                || ! content->getProperty ("sha256_pcm").isString())
                return false;
            result.sourceFileSha256 = content->getProperty ("sha256_file").toString();
            result.sourcePcmSha256 = content->getProperty ("sha256_pcm").toString();
            return sha256 (result.sourceFileSha256) && sha256 (result.sourcePcmSha256)
                && result.sourceFileSha256 == source.sourceFileSha256
                && result.sourcePcmSha256 == source.sourcePcmSha256
                && parseAudio (object->getProperty ("audio"), result.audio)
                && result.audio.sampleRateHz == source.audio.sampleRateHz
                && result.audio.channels == source.audio.channels
                && result.audio.totalSampleFrames == source.audio.totalSampleFrames
                && parseGrid (object->getProperty ("grid"), result.audio, result.grid)
                && parseFeatures (object->getProperty ("features"), result);
        }
    }

    RuntimeV2AlignmentRepository::RuntimeV2AlignmentRepository (juce::File transportRootIn)
        : root (std::move (transportRootIn))
    {
    }

    RuntimeAlignmentLoadResult RuntimeV2AlignmentRepository::load (const RuntimeSource& source) const
    {
        if (! source.alignmentArtifact)
            return { {}, "reference_alignment_unavailable" };
        const auto& receipt = *source.alignmentArtifact;
        if (! sha256 (receipt.sha256)
            || receipt.relativePath != "plugin_data/reference/v2/alignments/"
                                         + receipt.sha256 + ".json")
            return { {}, "reference_alignment_receipt_rejected" };
        juce::var json;
        if (! readJson (root.getChildFile ("alignments").getChildFile (receipt.sha256 + ".json"),
                        receipt, json))
            return { {}, "reference_alignment_receipt_rejected" };
        auto alignment = std::make_shared<RuntimeAlignment>();
        if (! parseAlignment (json, source, *alignment))
            return { {}, "reference_alignment_contract_rejected" };
        return { std::shared_ptr<const RuntimeAlignment> (std::move (alignment)), {} };
    }
}
