#include "ReferenceRuntimeV2Measurement.h"

#include <cmath>
#include <limits>
#include <regex>
#include <utility>

#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
    namespace
    {
        constexpr std::int64_t maximumBytes = 2 * 1024 * 1024;
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

        bool fullCoverage (size_t points, std::int64_t hop,
                           const RuntimeAudioFacts& audio, size_t maximumPoints)
        {
            return points >= 1 && points <= maximumPoints && hop >= 1
                && (audio.totalSampleFrames + hop - 1) / hop == static_cast<std::int64_t> (points);
        }

        bool parseIntegerSeries (const juce::var& value, size_t minimumItems, size_t maximumItems,
                                 std::int64_t minimum, std::int64_t maximum,
                                 std::vector<std::int64_t>& result)
        {
            const auto* array = value.getArray();
            if (array == nullptr || array->size() < static_cast<int> (minimumItems)
                || array->size() > static_cast<int> (maximumItems))
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

        bool parseNullableSeries (const juce::var& value, size_t expectedItems,
                                  std::int64_t minimum, std::int64_t maximum,
                                  RuntimeNullableIntegerSeries& result)
        {
            const auto* array = value.getArray();
            if (array == nullptr || array->size() != static_cast<int> (expectedItems))
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

        bool parseWaveform (const juce::var& value, const RuntimeAudioFacts& audio,
                            std::optional<RuntimeMeasurementWaveform>& output)
        {
            if (value.isVoid())
                return true;
            const auto* object = value.getDynamicObject();
            RuntimeMeasurementWaveform result;
            std::int64_t start = -1, bins = 0;
            const auto* peaks = object == nullptr ? nullptr
                : object->getProperty ("sample_peak_millidbfs").getArray();
            const auto* rms = object == nullptr ? nullptr
                : object->getProperty ("rms_millidbfs").getArray();
            if (object == nullptr || ! exactProperties (*object, {
                    "start_sample", "frames_per_bin", "bin_count",
                    "sample_peak_millidbfs", "rms_millidbfs" })
                || ! exactInteger (object->getProperty ("start_sample"), 0, 0, start)
                || ! exactInteger (object->getProperty ("bin_count"), 1, 4096, bins)
                || ! exactInteger (object->getProperty ("frames_per_bin"), 1,
                                   maximumSafeInteger, result.framesPerBin)
                || ! fullCoverage (static_cast<size_t> (bins), result.framesPerBin, audio, 4096)
                || peaks == nullptr || rms == nullptr
                || peaks->size() != audio.channels || rms->size() != audio.channels)
                return false;
            for (int channel = 0; channel < audio.channels; ++channel)
            {
                std::vector<std::int64_t> peakSeries, rmsSeries;
                if (! parseIntegerSeries ((*peaks)[channel], static_cast<size_t> (bins),
                                         static_cast<size_t> (bins), -300'000, 24'000, peakSeries)
                    || ! parseIntegerSeries ((*rms)[channel], static_cast<size_t> (bins),
                                            static_cast<size_t> (bins), -300'000, 24'000, rmsSeries))
                    return false;
                for (size_t index = 0; index < peakSeries.size(); ++index)
                    if (rmsSeries[index] > peakSeries[index])
                        return false;
                result.samplePeakMillidbfs.push_back (std::move (peakSeries));
                result.rmsMillidbfs.push_back (std::move (rmsSeries));
            }
            output = std::move (result);
            return true;
        }

        bool parseSpectrum (const juce::var& value, const RuntimeAudioFacts& audio,
                            std::optional<RuntimeMeasurementSpectrum>& output)
        {
            if (value.isVoid())
                return true;
            const auto* object = value.getDynamicObject();
            RuntimeMeasurementSpectrum result;
            const auto* centers = object == nullptr ? nullptr
                : object->getProperty ("band_centers_hz").getArray();
            if (object == nullptr || ! exactProperties (*object, {
                    "band_centers_hz", "p10_millidbfs", "median_millidbfs", "p90_millidbfs" })
                || centers == nullptr || centers->size() < 12 || centers->size() > 256)
                return false;
            double previous = 0.0;
            for (const auto& item : *centers)
            {
                if (! item.isDouble() && ! item.isInt() && ! item.isInt64())
                    return false;
                const auto center = static_cast<double> (item);
                if (! std::isfinite (center) || center <= previous
                    || center > static_cast<double> (audio.sampleRateHz) / 2.0)
                    return false;
                result.bandCentersHz.push_back (center);
                previous = center;
            }
            const auto count = result.bandCentersHz.size();
            if (! parseIntegerSeries (object->getProperty ("p10_millidbfs"), count, count,
                                     -300'000, 24'000, result.p10Millidbfs)
                || ! parseIntegerSeries (object->getProperty ("median_millidbfs"), count, count,
                                        -300'000, 24'000, result.medianMillidbfs)
                || ! parseIntegerSeries (object->getProperty ("p90_millidbfs"), count, count,
                                        -300'000, 24'000, result.p90Millidbfs))
                return false;
            for (size_t index = 0; index < count; ++index)
                if (result.p10Millidbfs[index] > result.medianMillidbfs[index]
                    || result.medianMillidbfs[index] > result.p90Millidbfs[index])
                    return false;
            output = std::move (result);
            return true;
        }

        bool parseTimeline (const juce::var& value, const RuntimeAudioFacts& audio,
                            const char* firstName, const char* secondName,
                            std::pair<std::int64_t, std::int64_t> firstBounds,
                            std::pair<std::int64_t, std::int64_t> secondBounds,
                            std::optional<RuntimeMeasurementTimeline>& output)
        {
            if (value.isVoid())
                return true;
            const auto* object = value.getDynamicObject();
            RuntimeMeasurementTimeline result;
            std::int64_t start = -1;
            const auto firstId = juce::Identifier (firstName);
            const auto secondId = juce::Identifier (secondName);
            const auto* first = object == nullptr ? nullptr : object->getProperty (firstId).getArray();
            if (object == nullptr || ! exactProperties (*object, {
                    "start_sample", "hop_samples", firstName, secondName })
                || ! exactInteger (object->getProperty ("start_sample"), 0, 0, start)
                || ! exactInteger (object->getProperty ("hop_samples"), 1,
                                   maximumSafeInteger, result.hopSamples)
                || first == nullptr || first->size() < 1 || first->size() > 8192
                || ! fullCoverage (static_cast<size_t> (first->size()), result.hopSamples, audio, 8192))
                return false;
            RuntimeNullableIntegerSeries firstSeries, secondSeries;
            if (! parseNullableSeries (object->getProperty (firstId), static_cast<size_t> (first->size()),
                                       firstBounds.first, firstBounds.second, firstSeries)
                || ! parseNullableSeries (object->getProperty (secondId), static_cast<size_t> (first->size()),
                                          secondBounds.first, secondBounds.second, secondSeries))
                return false;
            bool hasFacts = false;
            for (size_t index = 0; index < firstSeries.size(); ++index)
                hasFacts = hasFacts || firstSeries[index].has_value() || secondSeries[index].has_value();
            if (! hasFacts)
                return false;
            result.series.emplace (firstName, std::move (firstSeries));
            result.series.emplace (secondName, std::move (secondSeries));
            output = std::move (result);
            return true;
        }

        bool parseTransient (const juce::var& value, const RuntimeAudioFacts& audio,
                             std::optional<RuntimeMeasurementTransient>& output)
        {
            if (value.isVoid())
                return true;
            const auto* object = value.getDynamicObject();
            RuntimeMeasurementTransient result;
            std::int64_t start = -1;
            if (object == nullptr || ! exactProperties (*object, {
                    "start_sample", "hop_samples", "onset_strength_q15" })
                || ! exactInteger (object->getProperty ("start_sample"), 0, 0, start)
                || ! exactInteger (object->getProperty ("hop_samples"), 1,
                                   maximumSafeInteger, result.hopSamples)
                || ! parseIntegerSeries (object->getProperty ("onset_strength_q15"), 1, 8192,
                                        0, 32'767, result.onsetStrengthQ15)
                || ! fullCoverage (result.onsetStrengthQ15.size(), result.hopSamples, audio, 8192))
                return false;
            output = std::move (result);
            return true;
        }

        bool parseMeasurement (const juce::var& value, const RuntimeSource& source,
                               RuntimeDetailedMeasurement& result)
        {
            const auto* object = value.getDynamicObject();
            const auto* content = object == nullptr ? nullptr
                : object->getProperty ("source_content").getDynamicObject();
            juce::String fileHash, pcmHash;
            if (object == nullptr || ! exactProperties (*object, {
                    "format", "version", "source_content", "audio", "views" })
                || object->getProperty ("format") != "kirin_hypha_reference_measurement"
                || object->getProperty ("version") != "2.0"
                || content == nullptr || ! exactProperties (*content, { "sha256_file", "sha256_pcm" })
                || ! content->getProperty ("sha256_file").isString()
                || ! content->getProperty ("sha256_pcm").isString())
                return false;
            fileHash = content->getProperty ("sha256_file").toString();
            pcmHash = content->getProperty ("sha256_pcm").toString();
            if (! sha256 (fileHash) || ! sha256 (pcmHash)
                || fileHash != source.sourceFileSha256 || pcmHash != source.sourcePcmSha256
                || ! parseAudio (object->getProperty ("audio"), result.audio)
                || result.audio.sampleRateHz != source.audio.sampleRateHz
                || result.audio.channels != source.audio.channels
                || result.audio.totalSampleFrames != source.audio.totalSampleFrames)
                return false;
            result.sourceFileSha256 = fileHash;
            result.sourcePcmSha256 = pcmHash;
            const auto* views = object->getProperty ("views").getDynamicObject();
            return views != nullptr && exactProperties (*views, {
                    "waveform", "spectrum", "loudness", "dynamics", "transient", "stereo" })
                && (result.audio.channels != 1 || views->getProperty ("stereo").isVoid())
                && parseWaveform (views->getProperty ("waveform"), result.audio, result.waveform)
                && parseSpectrum (views->getProperty ("spectrum"), result.audio, result.spectrum)
                && parseTimeline (views->getProperty ("loudness"), result.audio,
                                 "lufs_m_millilu", "lufs_s_millilu",
                                 { -300'000, 100'000 }, { -300'000, 100'000 }, result.loudness)
                && parseTimeline (views->getProperty ("dynamics"), result.audio,
                                 "psr_millidb", "crest_millidb",
                                 { -300'000, 100'000 }, { -300'000, 100'000 }, result.dynamics)
                && parseTransient (views->getProperty ("transient"), result.audio, result.transient)
                && parseTimeline (views->getProperty ("stereo"), result.audio,
                                 "correlation_milli", "width_basis_points",
                                 { -1'000, 1'000 }, { 0, 15'000 }, result.stereo);
        }
    }

    RuntimeV2MeasurementRepository::RuntimeV2MeasurementRepository (juce::File transportRootIn)
        : root (std::move (transportRootIn))
    {
    }

    RuntimeMeasurementLoadResult RuntimeV2MeasurementRepository::load (
        const RuntimeSource& source) const
    {
        if (! source.measurementArtifact)
            return { {}, "reference_measurement_unavailable" };
        const auto& receipt = *source.measurementArtifact;
        if (! sha256 (receipt.sha256)
            || receipt.relativePath != "plugin_data/reference/v2/measurements/"
                                         + receipt.sha256 + ".json")
            return { {}, "reference_measurement_receipt_rejected" };
        juce::var json;
        if (! readJson (root.getChildFile ("measurements").getChildFile (receipt.sha256 + ".json"),
                        receipt, json))
            return { {}, "reference_measurement_receipt_rejected" };
        auto measurement = std::make_shared<RuntimeDetailedMeasurement>();
        if (! parseMeasurement (json, source, *measurement))
            return { {}, "reference_measurement_contract_rejected" };
        return { std::shared_ptr<const RuntimeDetailedMeasurement> (std::move (measurement)), {} };
    }
}
