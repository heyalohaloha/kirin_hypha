#include "ReferenceAuditionProtocol.h"

#include <cmath>
#include <regex>
#include <set>

namespace hypha::reference_audition
{
    namespace
    {
        constexpr double maxDurationSeconds = 7.0 * 24.0 * 60.0 * 60.0;
        constexpr std::int64_t maxSampleRateHz = 1'000'000;
        constexpr std::int64_t maxChannels = 256;
        constexpr std::int64_t maxSignaturePoints = 512;
        constexpr std::int64_t maxSourcePoints = 432'000;
        constexpr double maxSafeJsonInteger = 9'007'199'254'740'991.0;

        bool exactProperties (const juce::DynamicObject& object,
                              std::initializer_list<const char*> names)
        {
            std::set<std::string> expected;
            for (const auto* name : names)
                expected.emplace (name);
            const auto& properties = object.getProperties();
            if (properties.size() != static_cast<int> (expected.size()))
                return false;
            for (int index = 0; index < properties.size(); ++index)
                if (expected.find (properties.getName (index).toString().toStdString())
                    == expected.end())
                    return false;
            return true;
        }

        const juce::DynamicObject* childObject (const juce::DynamicObject& parent,
                                                const char* name)
        {
            return parent.getProperty (name).getDynamicObject();
        }

        juce::String stringProperty (const juce::DynamicObject& object, const char* name)
        {
            const auto value = object.getProperty (name);
            return value.isString() ? value.toString() : juce::String {};
        }

        bool nullProperty (const juce::DynamicObject& object, const char* name)
        {
            return object.getProperty (name).isVoid();
        }

        bool exactBoolean (const juce::DynamicObject& object, const char* name, bool expected)
        {
            const auto value = object.getProperty (name);
            return value.isBool() && static_cast<bool> (value) == expected;
        }

        bool finiteNumber (const juce::var& value, double minimum, double maximum, double& output)
        {
            if (! value.isDouble() && ! value.isInt() && ! value.isInt64())
                return false;
            const auto number = static_cast<double> (value);
            if (! std::isfinite (number) || number < minimum || number > maximum)
                return false;
            output = number;
            return true;
        }

        bool integerNumber (const juce::var& value, std::int64_t minimum,
                            std::int64_t maximum, std::int64_t& output)
        {
            double number = 0.0;
            if (! finiteNumber (value, static_cast<double> (minimum),
                                static_cast<double> (maximum), number)
                || std::floor (number) != number)
                return false;
            output = static_cast<std::int64_t> (number);
            return true;
        }

        bool cleanText (const juce::String& value, int maximumBytes = 160)
        {
            if (value.isEmpty()
                || value.getNumBytesAsUTF8() > static_cast<size_t> (maximumBytes))
                return false;
            for (auto character : value)
                if (character < 0x20 || character == 0x7f)
                    return false;
            return value.trim() == value && ! value.contains ("  ");
        }

        bool nullableSafeId (const juce::DynamicObject& object, const char* name,
                             juce::String& output)
        {
            if (nullProperty (object, name))
            {
                output.clear();
                return true;
            }
            output = stringProperty (object, name);
            return safeId (output);
        }

        bool nullableSha256 (const juce::DynamicObject& object, const char* name,
                             juce::String& output, bool& present)
        {
            if (nullProperty (object, name))
            {
                output.clear();
                present = false;
                return true;
            }
            output = stringProperty (object, name);
            present = safeSha256 (output);
            return present;
        }

        bool sourceIdentityValid (const juce::String& kind,
                                  const juce::String& workId,
                                  const juce::String& versionId,
                                  const juce::String& catalogId)
        {
            if (kind == "work_version")
                return safeId (workId) && safeId (versionId) && catalogId.isEmpty();
            if (kind == "catalog")
                return workId.isEmpty() && versionId.isEmpty() && safeId (catalogId);
            return false;
        }

        bool parseObservation (const juce::var& value, ObservationSignature& output,
                               bool& present)
        {
            if (value.isVoid())
            {
                present = false;
                return true;
            }
            const auto* signature = value.getDynamicObject();
            if (signature == nullptr || ! exactProperties (*signature, {
                    "kind", "source_hop_ms", "source_point_count", "downsample_stride",
                    "signature_hop_ms", "quantization_db", "values" }))
                return false;
            if (stringProperty (*signature, "kind") != "loudness_lufs_m")
                return false;
            double quantization = 0.0;
            if (! finiteNumber (signature->getProperty ("source_hop_ms"),
                               std::numeric_limits<double>::min(), maxDurationSeconds * 1000.0,
                               output.sourceHopMs)
                || ! integerNumber (signature->getProperty ("source_point_count"), 1,
                                    maxSourcePoints, output.sourcePointCount)
                || ! integerNumber (signature->getProperty ("downsample_stride"), 1,
                                    maxSourcePoints, output.downsampleStride)
                || ! finiteNumber (signature->getProperty ("signature_hop_ms"),
                                   std::numeric_limits<double>::min(),
                                   maxDurationSeconds * 1000.0, output.signatureHopMs)
                || ! finiteNumber (signature->getProperty ("quantization_db"), 0.01, 0.01,
                                   quantization))
                return false;
            if (std::abs (output.signatureHopMs
                          - output.sourceHopMs * static_cast<double> (output.downsampleStride))
                > std::numeric_limits<double>::epsilon()
                    * juce::jmax (1.0, output.signatureHopMs) * 4.0)
                return false;
            const auto* values = signature->getProperty ("values").getArray();
            const auto expected = (output.sourcePointCount + output.downsampleStride - 1)
                                / output.downsampleStride;
            if (values == nullptr || values->isEmpty() || values->size() > maxSignaturePoints
                || values->size() != expected)
                return false;
            output.values.clear();
            output.present.clear();
            bool any = false;
            for (const auto& item : *values)
            {
                if (item.isVoid())
                {
                    output.values.push_back (0.0);
                    output.present.push_back (false);
                    continue;
                }
                double number = 0.0;
                if (! finiteNumber (item, -200.0, 100.0, number)
                    || std::abs (number * 100.0 - std::round (number * 100.0)) > 1.0e-8)
                    return false;
                output.values.push_back (number);
                output.present.push_back (true);
                any = true;
            }
            present = any;
            return any;
        }
    }

    bool safeId (const juce::String& value) noexcept
    {
        static const std::regex pattern (R"(^[A-Za-z0-9._:-]{1,160}$)");
        return std::regex_match (value.toStdString(), pattern);
    }

    bool safeSha256 (const juce::String& value) noexcept
    {
        static const std::regex pattern (R"(^[a-f0-9]{64}$)");
        return std::regex_match (value.toStdString(), pattern);
    }

    bool safeUuid (const juce::String& value) noexcept
    {
        static const std::regex pattern (
            R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$)");
        return std::regex_match (value.toStdString(), pattern);
    }

    bool RuntimeIdentity::valid() const noexcept
    {
        return safeId (runtimeInstanceId) && safeId (workId) && hostProcessId > 0;
    }

    bool Preparation::valid() const noexcept
    {
        return safeUuid (preparationId) && safeId (workId) && safeId (sourceId)
            && sourceIdentityValid (sourceKind, sourceWorkId, versionId, catalogReferenceId)
            && cleanText (title) && cleanText (fileName) && safeSha256 (sourceSha256)
            && safeSha256 (receiptSha256) && receiptBytes > 0
            && receiptBytes <= maximumSourceReceiptBytes
            && std::isfinite (maxSafePositiveGainDb) && maxSafePositiveGainDb >= 0.0
            && maxSafePositiveGainDb <= 100.0 && preparedAtMs >= 0;
    }

    bool SourceReceipt::valid() const noexcept
    {
        return safeId (sourceId)
            && sourceIdentityValid (sourceKind, sourceWorkId, versionId, catalogReferenceId)
            && cleanText (title) && cleanText (fileName) && safeSha256 (sourceSha256)
            && juce::File::isAbsolutePath (filePath) && filePath.getNumBytesAsUTF8() <= 4096
            && std::isfinite (integratedLoudness) && integratedLoudness >= -100.0
            && integratedLoudness <= 24.0 && std::isfinite (maximumTruePeakDbtp)
            && maximumTruePeakDbtp >= -100.0 && maximumTruePeakDbtp <= 24.0;
    }

    bool SourceReceipt::matches (const Preparation& value) const noexcept
    {
        return sourceKind == value.sourceKind && sourceId == value.sourceId
            && sourceWorkId == value.sourceWorkId && versionId == value.versionId
            && catalogReferenceId == value.catalogReferenceId && title == value.title
            && fileName == value.fileName && sourceSha256 == value.sourceSha256;
    }

    bool parsePreparation (const juce::var& value, Preparation& output) noexcept
    {
        const auto* root = value.getDynamicObject();
        if (root == nullptr || ! exactProperties (*root, {
                "format", "version", "preparation_id", "target", "source",
                "source_receipt", "level_match", "alignment", "state", "prepared_at_ms" }))
            return false;
        if (stringProperty (*root, "format") != "kirin_hypha_ab_preparation"
            || stringProperty (*root, "version") != "1.2")
            return false;
        output = {};
        output.preparationId = stringProperty (*root, "preparation_id");
        const auto* target = childObject (*root, "target");
        const auto* source = childObject (*root, "source");
        const auto* receipt = childObject (*root, "source_receipt");
        const auto* level = childObject (*root, "level_match");
        const auto* alignment = childObject (*root, "alignment");
        const auto* state = childObject (*root, "state");
        if (target == nullptr || ! exactProperties (*target, { "work_id" })
            || source == nullptr || ! exactProperties (*source, {
                "kind", "source_id", "source_work_id", "version_id",
                "catalog_reference_id", "title", "file_name", "sha256_file" })
            || receipt == nullptr || ! exactProperties (*receipt, { "sha256", "bytes" })
            || level == nullptr || ! exactProperties (*level, {
                "policy", "true_peak_ceiling_dbtp", "max_safe_positive_gain_db",
                "positive_gain_allowed" })
            || alignment == nullptr || ! exactProperties (*alignment, {
                "requested_mode", "runtime_confirmation_required", "reference_cue_seconds" })
            || state == nullptr || ! exactProperties (*state, {
                "audible_source_on_open", "prepared_side", "audio_modified_by_os" }))
            return false;
        output.workId = stringProperty (*target, "work_id");
        output.sourceKind = stringProperty (*source, "kind");
        output.sourceId = stringProperty (*source, "source_id");
        if (! nullableSafeId (*source, "source_work_id", output.sourceWorkId)
            || ! nullableSafeId (*source, "version_id", output.versionId)
            || ! nullableSafeId (*source, "catalog_reference_id", output.catalogReferenceId))
            return false;
        output.title = stringProperty (*source, "title");
        output.fileName = stringProperty (*source, "file_name");
        output.sourceSha256 = stringProperty (*source, "sha256_file");
        output.receiptSha256 = stringProperty (*receipt, "sha256");
        if (! integerNumber (receipt->getProperty ("bytes"), 1,
                            maximumSourceReceiptBytes, output.receiptBytes)
            || stringProperty (*level, "policy") != "b_matches_a")
            return false;
        double ceiling = 0.0;
        if (! finiteNumber (level->getProperty ("true_peak_ceiling_dbtp"), -1.0, -1.0,
                           ceiling)
            || ! finiteNumber (level->getProperty ("max_safe_positive_gain_db"), 0.0, 100.0,
                               output.maxSafePositiveGainDb))
            return false;
        const auto gainAllowed = level->getProperty ("positive_gain_allowed");
        if (! gainAllowed.isBool()
            || static_cast<bool> (gainAllowed) != (output.maxSafePositiveGainDb > 0.0))
            return false;
        const auto requestedMode = stringProperty (*alignment, "requested_mode");
        if (requestedMode == "sample_lock")
        {
            output.alignmentMode = AlignmentMode::sampleLock;
            if (! nullProperty (*alignment, "reference_cue_seconds"))
                return false;
        }
        else if (requestedMode == "reference_cue")
        {
            output.alignmentMode = AlignmentMode::referenceCue;
            if (! finiteNumber (alignment->getProperty ("reference_cue_seconds"), 0.0,
                               maxDurationSeconds, output.referenceCueSeconds))
                return false;
        }
        else
            return false;
        if (! exactBoolean (*alignment, "runtime_confirmation_required", true)
            || stringProperty (*state, "audible_source_on_open") != "a"
            || stringProperty (*state, "prepared_side") != "b"
            || ! exactBoolean (*state, "audio_modified_by_os", false)
            || ! integerNumber (root->getProperty ("prepared_at_ms"), 0,
                               static_cast<std::int64_t> (maxSafeJsonInteger), output.preparedAtMs))
            return false;
        return output.valid();
    }

    bool parseSourceReceipt (const juce::var& value, SourceReceipt& output) noexcept
    {
        const auto* root = value.getDynamicObject();
        if (root == nullptr || ! exactProperties (*root, {
                "format", "version", "source", "measurement", "alignment_material" })
            || stringProperty (*root, "format") != "kirin_hypha_ab_source_receipt"
            || stringProperty (*root, "version") != "1.0")
            return false;
        const auto* source = childObject (*root, "source");
        const auto* measurement = childObject (*root, "measurement");
        const auto* material = childObject (*root, "alignment_material");
        if (source == nullptr || ! exactProperties (*source, {
                "kind", "source_id", "source_work_id", "version_id",
                "catalog_reference_id", "title", "file_name", "file_path", "sha256_file",
                "measurement_receipt_sha256", "revision" })
            || measurement == nullptr || ! exactProperties (*measurement, {
                "standard", "measured_at", "lufs_i", "max_true_peak_dbtp",
                "duration_seconds", "sample_rate_hz" })
            || material == nullptr || ! exactProperties (*material, {
                "clock", "content", "observation_signature", "runtime_correlation_required" }))
            return false;
        output = {};
        output.sourceKind = stringProperty (*source, "kind");
        output.sourceId = stringProperty (*source, "source_id");
        if (! nullableSafeId (*source, "source_work_id", output.sourceWorkId)
            || ! nullableSafeId (*source, "version_id", output.versionId)
            || ! nullableSafeId (*source, "catalog_reference_id", output.catalogReferenceId))
            return false;
        output.title = stringProperty (*source, "title");
        output.fileName = stringProperty (*source, "file_name");
        output.filePath = stringProperty (*source, "file_path");
        output.sourceSha256 = stringProperty (*source, "sha256_file");
        bool measurementHashPresent = false;
        if (! nullableSha256 (*source, "measurement_receipt_sha256",
                             output.measurementReceiptSha256, measurementHashPresent))
            return false;
        const auto* revision = childObject (*source, "revision");
        if (revision == nullptr || ! exactProperties (*revision, {
                "dev", "ino", "size", "mtime_ms", "ctime_ms" }))
            return false;
        output.revisionDevice = stringProperty (*revision, "dev");
        output.revisionInode = stringProperty (*revision, "ino");
        output.revisionSize = stringProperty (*revision, "size");
        output.revisionModifiedMs = stringProperty (*revision, "mtime_ms");
        output.revisionChangedMs = stringProperty (*revision, "ctime_ms");
        static const std::regex decimal (R"(^[0-9]+(?:\.[0-9]+)?$)");
        for (const auto& revisionValue : { output.revisionDevice, output.revisionInode,
                                           output.revisionSize, output.revisionModifiedMs,
                                           output.revisionChangedMs })
            if (! std::regex_match (revisionValue.toStdString(), decimal))
                return false;
        if (stringProperty (*measurement, "standard") != "ITU-R BS.1770"
            || stringProperty (*measurement, "measured_at").isEmpty()
            || ! finiteNumber (measurement->getProperty ("lufs_i"), -100.0, 24.0,
                               output.integratedLoudness)
            || ! finiteNumber (measurement->getProperty ("max_true_peak_dbtp"), -100.0, 24.0,
                               output.maximumTruePeakDbtp))
            return false;
        if (! nullProperty (*measurement, "duration_seconds"))
        {
            output.hasDuration = finiteNumber (measurement->getProperty ("duration_seconds"),
                                               std::numeric_limits<double>::min(),
                                               maxDurationSeconds, output.durationSeconds);
            if (! output.hasDuration)
                return false;
        }
        if (! nullProperty (*measurement, "sample_rate_hz"))
        {
            output.hasSampleRate = integerNumber (measurement->getProperty ("sample_rate_hz"),
                                                  1, maxSampleRateHz, output.sampleRateHz);
            if (! output.hasSampleRate)
                return false;
        }
        const auto* clock = childObject (*material, "clock");
        const auto* content = childObject (*material, "content");
        if (clock == nullptr || ! exactProperties (*clock, {
                "sample_rate_hz", "channels", "sample_count", "duration_seconds" })
            || content == nullptr || ! exactProperties (*content, { "sha256_pcm" })
            || ! exactBoolean (*material, "runtime_correlation_required", true))
            return false;
        std::int64_t clockSampleRate = 0;
        double clockDuration = 0.0;
        if (! nullProperty (*clock, "sample_rate_hz"))
        {
            output.hasSampleRate = integerNumber (clock->getProperty ("sample_rate_hz"), 1,
                                                  maxSampleRateHz, clockSampleRate);
            if (! output.hasSampleRate
                || (output.sampleRateHz != 0 && output.sampleRateHz != clockSampleRate))
                return false;
            output.sampleRateHz = clockSampleRate;
        }
        if (! nullProperty (*clock, "channels"))
        {
            output.hasChannels = integerNumber (clock->getProperty ("channels"), 1,
                                                maxChannels, output.channels);
            if (! output.hasChannels)
                return false;
        }
        if (! nullProperty (*clock, "sample_count"))
        {
            output.hasSampleCount = integerNumber (clock->getProperty ("sample_count"), 1,
                                                   static_cast<std::int64_t> (maxSafeJsonInteger),
                                                   output.sampleCount);
            if (! output.hasSampleCount)
                return false;
        }
        if (! nullProperty (*clock, "duration_seconds"))
        {
            if (! finiteNumber (clock->getProperty ("duration_seconds"),
                               std::numeric_limits<double>::min(), maxDurationSeconds,
                               clockDuration)
                || (output.hasDuration
                    && std::abs (clockDuration - output.durationSeconds)
                       > (output.hasSampleRate ? 1.0 / output.sampleRateHz : 0.001)))
                return false;
            output.durationSeconds = clockDuration;
            output.hasDuration = true;
        }
        if (output.hasSampleCount && output.hasSampleRate && output.hasDuration
            && std::abs (static_cast<double> (output.sampleCount)
                         - static_cast<double> (output.sampleRateHz) * output.durationSeconds) > 1.0)
            return false;
        if (! nullableSha256 (*content, "sha256_pcm", output.decodedPcmSha256,
                             output.hasDecodedPcmSha256)
            || ! parseObservation (material->getProperty ("observation_signature"),
                                  output.observation, output.hasObservation))
            return false;
        return output.valid();
    }
}
