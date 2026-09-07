#include "ReferenceRuntimeV2Source.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <regex>
#include <set>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <sys/stat.h>
#endif

namespace hypha::reference_audition
{
    namespace
    {
        constexpr std::int64_t maximumSourceBytes = 64 * 1024;
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

        bool pattern (const juce::String& value, const char* expression)
        {
            return std::regex_match (value.toStdString(), std::regex (expression));
        }

        bool uuidV4 (const juce::String& value)
        {
            return pattern (value, R"(^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$)");
        }

        bool sha256 (const juce::String& value)
        {
            return pattern (value, R"(^[a-f0-9]{64}$)");
        }

        bool uint64Text (const juce::var& value, juce::String& result, bool positive = false)
        {
            if (! exactString (value, result)
                || ! pattern (result, R"(^(0|[1-9][0-9]{0,19})$)"))
                return false;
            std::uint64_t parsed = 0;
            const auto text = result.toStdString();
            const auto converted = std::from_chars (text.data(), text.data() + text.size(), parsed);
            return converted.ec == std::errc() && converted.ptr == text.data() + text.size()
                && (! positive || parsed > 0);
        }

        bool readJson (const juce::File& file, const RuntimeContentReceipt& receipt,
                       juce::var& value)
        {
            if (! file.existsAsFile() || file.isSymbolicLink()
                || file.getSize() != receipt.bytes || receipt.bytes < 1
                || receipt.bytes > maximumSourceBytes)
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

        bool parseContentReceipt (const juce::var& value, const juce::String& kind,
                                  std::int64_t maximumBytes, RuntimeContentReceipt& result)
        {
            const auto* object = value.getDynamicObject();
            return object != nullptr
                && exactProperties (*object, { "relative_path", "sha256", "bytes" })
                && exactString (object->getProperty ("relative_path"), result.relativePath)
                && exactString (object->getProperty ("sha256"), result.sha256)
                && sha256 (result.sha256)
                && result.relativePath == "plugin_data/reference/v2/" + kind + "/"
                                             + result.sha256 + ".json"
                && exactInteger (object->getProperty ("bytes"), 1, maximumBytes, result.bytes);
        }

        bool parseIdentity (const juce::var& value, const juce::String& kind,
                            juce::String& identityKey, juce::String& fileHash,
                            juce::String& pcmHash)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr)
                return false;
            if (kind == "work_version")
            {
                juce::String work, recording, version;
                if (! exactProperties (*object, { "work_id", "recording_id", "version_id", "sha256_file", "sha256_pcm" })
                    || ! exactString (object->getProperty ("work_id"), work)
                    || ! exactString (object->getProperty ("recording_id"), recording)
                    || ! exactString (object->getProperty ("version_id"), version)
                    || ! exactString (object->getProperty ("sha256_file"), fileHash)
                    || ! exactString (object->getProperty ("sha256_pcm"), pcmHash)
                    || ! uuidV4 (work) || ! uuidV4 (recording) || ! uuidV4 (version)
                    || ! sha256 (fileHash) || ! sha256 (pcmHash))
                    return false;
                identityKey = work + ":" + recording + ":" + version + ":" + fileHash + ":" + pcmHash;
                return true;
            }
            if (kind == "catalog_track")
            {
                juce::String catalog;
                if (! exactProperties (*object, { "catalog_reference_id", "sha256_file", "sha256_pcm" })
                    || ! exactString (object->getProperty ("catalog_reference_id"), catalog)
                    || ! exactString (object->getProperty ("sha256_file"), fileHash)
                    || ! exactString (object->getProperty ("sha256_pcm"), pcmHash)
                    || ! pattern (catalog, R"(^[a-z0-9][a-z0-9._:-]{0,127}$)")
                    || ! sha256 (fileHash) || ! sha256 (pcmHash))
                    return false;
                identityKey = catalog + ":" + fileHash + ":" + pcmHash;
                return true;
            }
            return false;
        }

        bool parseFile (const juce::var& value, RuntimeSource& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, { "absolute_path", "revision" })
                || ! exactString (object->getProperty ("absolute_path"), result.absolutePath)
                || result.absolutePath.isEmpty()
                || result.absolutePath.getNumBytesAsUTF8() > 4096
                || ! juce::File::isAbsolutePath (result.absolutePath)
                || result.absolutePath.containsChar ('\0'))
                return false;
            const auto* revision = object->getProperty ("revision").getDynamicObject();
            return revision != nullptr
                && exactProperties (*revision, { "device_id", "file_id", "size_bytes", "mtime_ns", "ctime_ns" })
                && uint64Text (revision->getProperty ("device_id"), result.revision.deviceId)
                && uint64Text (revision->getProperty ("file_id"), result.revision.fileId)
                && uint64Text (revision->getProperty ("size_bytes"), result.revision.sizeBytes, true)
                && uint64Text (revision->getProperty ("mtime_ns"), result.revision.modifiedTimeNs)
                && uint64Text (revision->getProperty ("ctime_ns"), result.revision.changedTimeNs);
        }

        bool parseAudio (const juce::var& value, RuntimeAudioFacts& result)
        {
            const auto* object = value.getDynamicObject();
            return object != nullptr
                && exactProperties (*object, { "sample_rate_hz", "channels", "total_sample_frames" })
                && exactInteger (object->getProperty ("sample_rate_hz"), 8'000, 768'000, result.sampleRateHz)
                && exactInteger (object->getProperty ("channels"), 1, 2, result.channels)
                && exactInteger (object->getProperty ("total_sample_frames"), 1,
                                 maximumSafeInteger, result.totalSampleFrames);
        }

        bool nullableNumber (const juce::var& value, double minimum, double maximum,
                             std::optional<double>& result)
        {
            if (value.isVoid())
            {
                result.reset();
                return true;
            }
            if (! value.isDouble() && ! value.isInt() && ! value.isInt64())
                return false;
            const auto number = static_cast<double> (value);
            if (! std::isfinite (number) || number < minimum || number > maximum)
                return false;
            result = number;
            return true;
        }

        bool parseSummary (const juce::var& value,
                           std::optional<RuntimeMeasurementSummary>& output)
        {
            if (value.isVoid())
            {
                output.reset();
                return true;
            }
            const auto* object = value.getDynamicObject();
            RuntimeMeasurementSummary result;
            if (object == nullptr || ! exactProperties (*object, {
                    "measured_at", "loudness_standard", "lufs_i", "max_true_peak_dbtp",
                    "lra_lu", "psr_mean_db", "crest_factor_db", "stereo_width_pct" })
                || ! exactString (object->getProperty ("measured_at"), result.measuredAt)
                || ! pattern (result.measuredAt, R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}Z$)")
                || object->getProperty ("loudness_standard") != "itu_r_bs_1770"
                || ! nullableNumber (object->getProperty ("lufs_i"), -100.0, 24.0, result.loudnessLufsI)
                || ! nullableNumber (object->getProperty ("max_true_peak_dbtp"), -100.0, 24.0, result.maximumTruePeakDbtp)
                || ! nullableNumber (object->getProperty ("lra_lu"), 0.0, 100.0, result.loudnessRangeLu)
                || ! nullableNumber (object->getProperty ("psr_mean_db"), -100.0, 100.0, result.meanPsrDb)
                || ! nullableNumber (object->getProperty ("crest_factor_db"), -100.0, 100.0, result.crestFactorDb)
                || ! nullableNumber (object->getProperty ("stereo_width_pct"), 0.0, 150.0, result.stereoWidthPercent))
                return false;
            if (! result.loudnessLufsI && ! result.maximumTruePeakDbtp && ! result.loudnessRangeLu
                && ! result.meanPsrDb && ! result.crestFactorDb && ! result.stereoWidthPercent)
                return false;
            output = std::move (result);
            return true;
        }

        bool parseMeasurement (const juce::var& value, RuntimeSource& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, { "summary", "detail_artifact" })
                || ! parseSummary (object->getProperty ("summary"), result.measurementSummary))
                return false;
            const auto detail = object->getProperty ("detail_artifact");
            if (detail.isVoid())
                return true;
            RuntimeContentReceipt receipt;
            if (! parseContentReceipt (detail, "measurements", 2 * 1024 * 1024, receipt))
                return false;
            result.measurementArtifact = std::move (receipt);
            return true;
        }

        bool parseSource (const juce::var& value, const RuntimeCandidate& candidate,
                          RuntimeSource& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, {
                    "format", "version", "source_kind", "source_identity", "file", "audio",
                    "measurement", "alignment" })
                || object->getProperty ("format") != "kirin_hypha_reference_source"
                || object->getProperty ("version") != "2.0"
                || ! exactString (object->getProperty ("source_kind"), result.sourceKind)
                || ! parseIdentity (object->getProperty ("source_identity"), result.sourceKind,
                                   result.sourceIdentityKey, result.sourceFileSha256,
                                   result.sourcePcmSha256)
                || result.sourceKind != candidate.sourceKind
                || result.sourceIdentityKey != candidate.sourceIdentityKey
                || ! parseFile (object->getProperty ("file"), result)
                || ! parseAudio (object->getProperty ("audio"), result.audio)
                || ! parseMeasurement (object->getProperty ("measurement"), result))
                return false;
            const auto alignment = object->getProperty ("alignment");
            if (! alignment.isVoid())
            {
                RuntimeContentReceipt receipt;
                if (! parseContentReceipt (alignment, "alignments", 512 * 1024, receipt))
                    return false;
                result.alignmentArtifact = std::move (receipt);
            }
            for (const auto& cue : candidate.cues)
                if (cue.sampleRateHz != result.audio.sampleRateHz
                    || cue.endSample > result.audio.totalSampleFrames)
                    return false;
            return true;
        }

        juce::String unsignedText (std::uint64_t value)
        {
            return juce::String (std::to_string (value));
        }

        bool currentRevision (const juce::File& file, RuntimeFileRevision& result)
        {
           #if JUCE_WINDOWS
            const auto path = file.getFullPathName().toWideCharPointer();
            const auto handle = CreateFileW (path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                             OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                return false;
            BY_HANDLE_FILE_INFORMATION info {};
            const bool ok = GetFileInformationByHandle (handle, &info) != 0
                && (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0
                && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
            CloseHandle (handle);
            if (! ok)
                return false;
            const auto combine = [] (DWORD high, DWORD low) {
                return (static_cast<std::uint64_t> (high) << 32) | low;
            };
            result.deviceId = unsignedText (info.dwVolumeSerialNumber);
            result.fileId = unsignedText (combine (info.nFileIndexHigh, info.nFileIndexLow));
            result.sizeBytes = unsignedText (combine (info.nFileSizeHigh, info.nFileSizeLow));
            result.modifiedTimeNs = unsignedText (combine (info.ftLastWriteTime.dwHighDateTime,
                                                           info.ftLastWriteTime.dwLowDateTime) * 100);
            result.changedTimeNs = unsignedText (combine (info.ftCreationTime.dwHighDateTime,
                                                          info.ftCreationTime.dwLowDateTime) * 100);
            return true;
           #else
            struct stat info {};
            if (::lstat (file.getFullPathName().toRawUTF8(), &info) != 0 || ! S_ISREG (info.st_mode))
                return false;
            result.deviceId = unsignedText (static_cast<std::uint64_t> (info.st_dev));
            result.fileId = unsignedText (static_cast<std::uint64_t> (info.st_ino));
            result.sizeBytes = unsignedText (static_cast<std::uint64_t> (info.st_size));
           #if JUCE_MAC
            result.modifiedTimeNs = unsignedText (static_cast<std::uint64_t> (info.st_mtimespec.tv_sec) * 1'000'000'000
                                                  + static_cast<std::uint64_t> (info.st_mtimespec.tv_nsec));
            result.changedTimeNs = unsignedText (static_cast<std::uint64_t> (info.st_ctimespec.tv_sec) * 1'000'000'000
                                                 + static_cast<std::uint64_t> (info.st_ctimespec.tv_nsec));
           #else
            result.modifiedTimeNs = unsignedText (static_cast<std::uint64_t> (info.st_mtim.tv_sec) * 1'000'000'000
                                                  + static_cast<std::uint64_t> (info.st_mtim.tv_nsec));
            result.changedTimeNs = unsignedText (static_cast<std::uint64_t> (info.st_ctim.tv_sec) * 1'000'000'000
                                                 + static_cast<std::uint64_t> (info.st_ctim.tv_nsec));
           #endif
            return true;
           #endif
        }

        bool sameRevision (const RuntimeFileRevision& left, const RuntimeFileRevision& right)
        {
            return left.deviceId == right.deviceId && left.fileId == right.fileId
                && left.sizeBytes == right.sizeBytes
                && left.modifiedTimeNs == right.modifiedTimeNs
                && left.changedTimeNs == right.changedTimeNs;
        }
    }

    RuntimeV2SourceRepository::RuntimeV2SourceRepository (juce::File transportRootIn)
        : root (std::move (transportRootIn))
    {
    }

    RuntimeSourceLoadResult RuntimeV2SourceRepository::load (
        const RuntimeCandidate& candidate) const
    {
        if (! sha256 (candidate.sourceArtifact.sha256)
            || candidate.sourceArtifact.relativePath != "plugin_data/reference/v2/sources/"
                                                       + candidate.sourceArtifact.sha256 + ".json")
            return { {}, "reference_source_receipt_rejected" };
        const auto file = root.getChildFile ("sources")
                              .getChildFile (candidate.sourceArtifact.sha256 + ".json");
        juce::var json;
        if (! readJson (file, candidate.sourceArtifact, json))
            return { {}, "reference_source_receipt_rejected" };
        auto source = std::make_shared<RuntimeSource>();
        if (! parseSource (json, candidate, *source))
            return { {}, "reference_source_contract_rejected" };
        return { std::shared_ptr<const RuntimeSource> (std::move (source)), {} };
    }

    juce::String RuntimeV2SourceRepository::verifySourceRevision (
        const RuntimeSource& source) const
    {
        const juce::File file (source.absolutePath);
        RuntimeFileRevision current;
        if (! currentRevision (file, current))
            return "reference_source_open_failed";
        if (! sameRevision (current, source.revision))
            return "reference_source_changed";
        return {};
    }

    juce::String RuntimeV2SourceRepository::verifySourceFile (const RuntimeSource& source) const
    {
        const juce::File file (source.absolutePath);
        const auto revisionFailure = verifySourceRevision (source);
        if (revisionFailure.isNotEmpty())
            return revisionFailure;
        if (juce::SHA256 (file).toHexString() != source.sourceFileSha256)
            return "reference_source_changed";
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr
            || static_cast<std::int64_t> (std::llround (reader->sampleRate)) != source.audio.sampleRateHz
            || reader->numChannels != source.audio.channels
            || reader->lengthInSamples != source.audio.totalSampleFrames)
            return "reference_source_audio_mismatch";
        return {};
    }
}
