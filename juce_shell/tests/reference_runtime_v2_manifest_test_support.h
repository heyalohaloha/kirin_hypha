#pragma once

#include "reference_runtime_blind_test_support.h"

namespace
{
    [[maybe_unused]] juce::var makeRuntimeV2Preset (const juce::String& presetId,
                                   const juce::String& revisionId)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_hypha_reference_preset");
        root->setProperty ("version", "2.0");
        root->setProperty ("work_id", workId);
        auto sourcePreset = new juce::DynamicObject();
        sourcePreset->setProperty ("preset_id", presetId);
        sourcePreset->setProperty ("revision_id", revisionId);
        sourcePreset->setProperty ("relative_path", "reference/presets/" + presetId
                                                     + "/" + revisionId + ".v1.json");
        sourcePreset->setProperty ("sha256", juce::String::repeatedString ("a", 64));
        sourcePreset->setProperty ("bytes", static_cast<juce::int64> (1024));
        root->setProperty ("source_preset_artifact", juce::var (sourcePreset));
        root->setProperty ("name", "Mix Reference");

        auto check = new juce::DynamicObject();
        check->setProperty ("check_id", "55555555-5555-4555-8555-555555555555");
        check->setProperty ("label", juce::String::fromUTF8 ("低音"));
        check->setProperty ("mode", "audition_with_facts");
        juce::Array<juce::var> views;
        views.add ("spectrum_low");
        check->setProperty ("view_bindings", juce::var (views));
        check->setProperty ("comparison_mode", "loudness_match");

        auto candidate = new juce::DynamicObject();
        candidate->setProperty ("candidate_id", "66666666-6666-4666-8666-666666666666");
        candidate->setProperty ("display_name", "Reference Mix");
        candidate->setProperty ("source_kind", "catalog_track");
        auto identity = new juce::DynamicObject();
        identity->setProperty ("catalog_reference_id", "catalog:reference-1");
        identity->setProperty ("sha256_file", juce::String::repeatedString ("b", 64));
        identity->setProperty ("sha256_pcm", juce::String::repeatedString ("c", 64));
        candidate->setProperty ("source_identity", juce::var (identity));
        auto sourceArtifact = new juce::DynamicObject();
        sourceArtifact->setProperty ("relative_path", "plugin_data/reference/v2/sources/"
            + juce::String::repeatedString ("d", 64) + ".json");
        sourceArtifact->setProperty ("sha256", juce::String::repeatedString ("d", 64));
        sourceArtifact->setProperty ("bytes", static_cast<juce::int64> (1024));
        candidate->setProperty ("source_artifact", juce::var (sourceArtifact));
        auto cue = new juce::DynamicObject();
        cue->setProperty ("cue_id", "77777777-7777-4777-8777-777777777777");
        cue->setProperty ("label", "Chorus");
        cue->setProperty ("sample_rate_hz", static_cast<juce::int64> (48'000));
        cue->setProperty ("start_sample", static_cast<juce::int64> (96'000));
        cue->setProperty ("end_sample", static_cast<juce::int64> (384'000));
        cue->setProperty ("loop_enabled", true);
        juce::Array<juce::var> cues;
        cues.add (juce::var (cue));
        candidate->setProperty ("cues", juce::var (cues));
        candidate->setProperty ("default_cue_id", "77777777-7777-4777-8777-777777777777");
        juce::Array<juce::var> candidates;
        candidates.add (juce::var (candidate));
        check->setProperty ("candidates", juce::var (candidates));
        check->setProperty ("profile_bindings", juce::var (juce::Array<juce::var> {}));
        juce::Array<juce::var> checks;
        checks.add (juce::var (check));
        root->setProperty ("checks", juce::var (checks));
        return juce::var (root);
    }

    [[maybe_unused]] juce::var makeRuntimeV2Manifest (const juce::String& presetId,
                                     const juce::String& revisionId,
                                     const juce::File& presetFile,
                                     std::int64_t revision)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_hypha_reference_manifest");
        root->setProperty ("version", "2.0");
        root->setProperty ("work_id", workId);
        root->setProperty ("revision", revision);
        auto state = new juce::DynamicObject();
        const auto stateHash = juce::String::repeatedString ("e", 64);
        state->setProperty ("relative_path", "reference/states/" + stateHash + ".v1.json");
        state->setProperty ("sha256", stateHash);
        state->setProperty ("bytes", static_cast<juce::int64> (2048));
        root->setProperty ("source_state_artifact", juce::var (state));
        auto active = new juce::DynamicObject();
        active->setProperty ("preset_id", presetId);
        active->setProperty ("revision_id", revisionId);
        root->setProperty ("active_preset", juce::var (active));
        auto receipt = new juce::DynamicObject();
        receipt->setProperty ("preset_id", presetId);
        receipt->setProperty ("revision_id", revisionId);
        receipt->setProperty ("relative_path", "plugin_data/reference/v2/presets/"
                                                + workId + "/" + presetId + ".json");
        receipt->setProperty ("sha256", juce::SHA256 (presetFile).toHexString());
        receipt->setProperty ("bytes", presetFile.getSize());
        juce::Array<juce::var> receipts;
        receipts.add (juce::var (receipt));
        root->setProperty ("preset_artifacts", juce::var (receipts));
        return juce::var (root);
    }

    [[maybe_unused]] juce::var makeRuntimeV2FileRevision (const juce::File& file)
    {
        auto revision = new juce::DynamicObject();
        const auto text = [] (std::uint64_t value) {
            return juce::String (std::to_string (value));
        };
       #if JUCE_WINDOWS
        const auto handle = CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_READ,
                                         FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                         FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        require (handle != INVALID_HANDLE_VALUE, "source fixture handle must open");
        BY_HANDLE_FILE_INFORMATION info {};
        require (GetFileInformationByHandle (handle, &info) != 0,
                 "source fixture revision must be readable");
        CloseHandle (handle);
        const auto combine = [] (DWORD high, DWORD low) {
            return (static_cast<std::uint64_t> (high) << 32) | low;
        };
        revision->setProperty ("device_id", text (info.dwVolumeSerialNumber));
        revision->setProperty ("file_id", text (combine (info.nFileIndexHigh, info.nFileIndexLow)));
        revision->setProperty ("size_bytes", text (combine (info.nFileSizeHigh, info.nFileSizeLow)));
        revision->setProperty ("mtime_ns", text (combine (info.ftLastWriteTime.dwHighDateTime,
                                                          info.ftLastWriteTime.dwLowDateTime) * 100));
        revision->setProperty ("ctime_ns", text (combine (info.ftCreationTime.dwHighDateTime,
                                                          info.ftCreationTime.dwLowDateTime) * 100));
       #else
        struct stat info {};
        require (::lstat (file.getFullPathName().toRawUTF8(), &info) == 0,
                 "source fixture revision must be readable");
        revision->setProperty ("device_id", text (static_cast<std::uint64_t> (info.st_dev)));
        revision->setProperty ("file_id", text (static_cast<std::uint64_t> (info.st_ino)));
        revision->setProperty ("size_bytes", text (static_cast<std::uint64_t> (info.st_size)));
       #if JUCE_MAC
        revision->setProperty ("mtime_ns", text (static_cast<std::uint64_t> (info.st_mtimespec.tv_sec) * 1'000'000'000
                                                 + static_cast<std::uint64_t> (info.st_mtimespec.tv_nsec)));
        revision->setProperty ("ctime_ns", text (static_cast<std::uint64_t> (info.st_ctimespec.tv_sec) * 1'000'000'000
                                                 + static_cast<std::uint64_t> (info.st_ctimespec.tv_nsec)));
       #else
        revision->setProperty ("mtime_ns", text (static_cast<std::uint64_t> (info.st_mtim.tv_sec) * 1'000'000'000
                                                 + static_cast<std::uint64_t> (info.st_mtim.tv_nsec)));
        revision->setProperty ("ctime_ns", text (static_cast<std::uint64_t> (info.st_ctim.tv_sec) * 1'000'000'000
                                                 + static_cast<std::uint64_t> (info.st_ctim.tv_nsec)));
       #endif
       #endif
        return juce::var (revision);
    }

    [[maybe_unused]] juce::var makeRuntimeV2Source (const juce::File& file, const juce::String& fileHash,
                                   const juce::String& pcmHash)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_hypha_reference_source");
        root->setProperty ("version", "2.0");
        root->setProperty ("source_kind", "catalog_track");
        auto identity = new juce::DynamicObject();
        identity->setProperty ("catalog_reference_id", "catalog:source-test");
        identity->setProperty ("sha256_file", fileHash);
        identity->setProperty ("sha256_pcm", pcmHash);
        root->setProperty ("source_identity", juce::var (identity));
        auto sourceFile = new juce::DynamicObject();
        sourceFile->setProperty ("absolute_path", file.getFullPathName());
        sourceFile->setProperty ("revision", makeRuntimeV2FileRevision (file));
        root->setProperty ("file", juce::var (sourceFile));
        auto audio = new juce::DynamicObject();
        audio->setProperty ("sample_rate_hz", static_cast<juce::int64> (48'000));
        audio->setProperty ("channels", static_cast<juce::int64> (2));
        audio->setProperty ("total_sample_frames", static_cast<juce::int64> (96'000));
        root->setProperty ("audio", juce::var (audio));
        auto measurement = new juce::DynamicObject();
        measurement->setProperty ("summary", juce::var());
        measurement->setProperty ("detail_artifact", juce::var());
        root->setProperty ("measurement", juce::var (measurement));
        root->setProperty ("alignment", juce::var());
        return juce::var (root);
    }

    [[maybe_unused]] juce::var bindRuntimeV2PresetToSource (const juce::String& presetId,
                                           const juce::String& revisionId,
                                           const ref::RuntimeContentReceipt& receipt,
                                           const juce::String& fileHash,
                                           const juce::String& pcmHash)
    {
        auto preset = makeRuntimeV2Preset (presetId, revisionId);
        auto* root = preset.getDynamicObject();
        auto* checks = root->getProperty ("checks").getArray();
        auto* check = checks->getReference (0).getDynamicObject();
        auto* candidates = check->getProperty ("candidates").getArray();
        auto* candidate = candidates->getReference (0).getDynamicObject();
        auto identity = new juce::DynamicObject();
        identity->setProperty ("catalog_reference_id", "catalog:source-test");
        identity->setProperty ("sha256_file", fileHash);
        identity->setProperty ("sha256_pcm", pcmHash);
        candidate->setProperty ("source_identity", juce::var (identity));
        auto sourceArtifact = new juce::DynamicObject();
        sourceArtifact->setProperty ("relative_path", receipt.relativePath);
        sourceArtifact->setProperty ("sha256", receipt.sha256);
        sourceArtifact->setProperty ("bytes", receipt.bytes);
        candidate->setProperty ("source_artifact", juce::var (sourceArtifact));
        auto* cues = candidate->getProperty ("cues").getArray();
        auto* cue = cues->getReference (0).getDynamicObject();
        cue->setProperty ("start_sample", static_cast<juce::int64> (0));
        cue->setProperty ("end_sample", static_cast<juce::int64> (96'000));
        return preset;
    }

    [[maybe_unused]] juce::var makeRuntimeV2WorkVersionSource (
        const juce::File& file, const juce::String& fileHash,
        const juce::String& pcmHash, const juce::String& recordingId,
        const juce::String& versionId, std::int64_t totalFrames)
    {
        auto source = makeRuntimeV2Source (file, fileHash, pcmHash);
        auto* root = source.getDynamicObject();
        root->setProperty ("source_kind", "work_version");
        auto identity = new juce::DynamicObject();
        identity->setProperty ("work_id", workId);
        identity->setProperty ("recording_id", recordingId);
        identity->setProperty ("version_id", versionId);
        identity->setProperty ("sha256_file", fileHash);
        identity->setProperty ("sha256_pcm", pcmHash);
        root->setProperty ("source_identity", juce::var (identity));
        root->getProperty ("audio").getDynamicObject()->setProperty (
            "total_sample_frames", totalFrames);
        return source;
    }

    [[maybe_unused]] juce::var bindRuntimeV2WorkVersionPresetToSource (
        const juce::String& presetId, const juce::String& revisionId,
        const ref::RuntimeContentReceipt& receipt, const juce::String& fileHash,
        const juce::String& pcmHash, const juce::String& recordingId,
        const juce::String& versionId, std::int64_t totalFrames)
    {
        auto preset = bindRuntimeV2PresetToSource (
            presetId, revisionId, receipt, fileHash, pcmHash);
        auto* candidate = preset.getDynamicObject()->getProperty ("checks").getArray()
                              ->getReference (0).getDynamicObject()
                              ->getProperty ("candidates").getArray()
                              ->getReference (0).getDynamicObject();
        candidate->setProperty ("source_kind", "work_version");
        auto identity = new juce::DynamicObject();
        identity->setProperty ("work_id", workId);
        identity->setProperty ("recording_id", recordingId);
        identity->setProperty ("version_id", versionId);
        identity->setProperty ("sha256_file", fileHash);
        identity->setProperty ("sha256_pcm", pcmHash);
        candidate->setProperty ("source_identity", juce::var (identity));
        candidate->getProperty ("cues").getArray()->getReference (0)
            .getDynamicObject()->setProperty ("end_sample", totalFrames);
        return preset;
    }

    [[maybe_unused]] void addRuntimeV2MeasurementSummary (juce::var& source, double loudness,
                                         double truePeak)
    {
        auto summary = new juce::DynamicObject();
        summary->setProperty ("measured_at", "2026-09-05T00:00:00.000Z");
        summary->setProperty ("loudness_standard", "itu_r_bs_1770");
        summary->setProperty ("lufs_i", loudness);
        summary->setProperty ("max_true_peak_dbtp", truePeak);
        summary->setProperty ("lra_lu", juce::var());
        summary->setProperty ("psr_mean_db", juce::var());
        summary->setProperty ("crest_factor_db", juce::var());
        summary->setProperty ("stereo_width_pct", juce::var());
        source.getDynamicObject()->getProperty ("measurement").getDynamicObject()
            ->setProperty ("summary", juce::var (summary));
    }

}
