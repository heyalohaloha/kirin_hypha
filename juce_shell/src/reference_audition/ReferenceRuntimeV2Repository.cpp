#include "ReferenceRuntimeV2Repository.h"

#include <limits>
#include <regex>
#include <set>
#include <utility>

#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
    namespace
    {
        constexpr std::int64_t maximumManifestBytes = 64 * 1024;
        constexpr std::int64_t maximumPresetBytes = 2 * 1024 * 1024;
        constexpr std::int64_t maximumSourceStateBytes = 1024 * 1024;
        constexpr std::int64_t maximumSourcePresetBytes = 8 * 1024 * 1024;
        constexpr std::int64_t maximumSafeInteger = 9'007'199'254'740'991;

        bool matches (const juce::String& value, const char* expression)
        {
            return std::regex_match (value.toStdString(), std::regex (expression));
        }

        bool workUuid (const juce::String& value)
        {
            return matches (value, R"(^[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}$)");
        }

        bool uuidV4 (const juce::String& value)
        {
            return matches (value, R"(^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$)");
        }

        bool sha256 (const juce::String& value)
        {
            return matches (value, R"(^[a-f0-9]{64}$)");
        }

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

        bool exactString (const juce::var& value, juce::String& result)
        {
            if (! value.isString())
                return false;
            result = value.toString();
            return true;
        }

        bool displayText (const juce::var& value, int maximumCharacters,
                          juce::String& result)
        {
            if (! exactString (value, result) || result.isEmpty()
                || result.length() > maximumCharacters || result.trim() != result)
                return false;
            for (auto character : result)
                if (character < 0x20 || (character >= 0x7f && character <= 0x9f)
                    || character == 0x2028 || character == 0x2029)
                    return false;
            return true;
        }

        bool readJson (const juce::File& file, std::int64_t maximumBytes,
                       juce::MemoryBlock& bytes, juce::var& value)
        {
            if (! file.existsAsFile() || file.isSymbolicLink())
                return false;
            const auto size = file.getSize();
            if (size < 1 || size > maximumBytes || size > std::numeric_limits<int>::max())
                return false;
            auto stream = file.createInputStream();
            if (stream == nullptr || ! stream->openedOk())
                return false;
            bytes.setSize (static_cast<size_t> (size), false);
            if (stream->read (bytes.getData(), static_cast<int> (size)) != size)
                return false;
            const auto* raw = static_cast<const char*> (bytes.getData());
            if (size >= 3 && static_cast<unsigned char> (raw[0]) == 0xef
                && static_cast<unsigned char> (raw[1]) == 0xbb
                && static_cast<unsigned char> (raw[2]) == 0xbf)
                return false;
            if (! juce::CharPointer_UTF8::isValidString (raw, static_cast<int> (size)))
                return false;
            value = juce::JSON::parse (juce::String::fromUTF8 (raw, static_cast<int> (size)));
            return ! value.isVoid();
        }

        bool parseContentReceipt (const juce::var& value, const juce::String& kind,
                                  std::int64_t maximumBytes, RuntimeContentReceipt& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, { "relative_path", "sha256", "bytes" })
                || ! exactString (object->getProperty ("relative_path"), result.relativePath)
                || ! exactString (object->getProperty ("sha256"), result.sha256)
                || ! sha256 (result.sha256)
                || result.relativePath != "plugin_data/reference/v2/" + kind + "/" + result.sha256 + ".json"
                || ! exactInteger (object->getProperty ("bytes"), 1, maximumBytes, result.bytes))
                return false;
            return true;
        }

        bool parseSourcePresetReceipt (const juce::var& value,
                                       RuntimeSourcePresetReceipt& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, {
                    "preset_id", "revision_id", "relative_path", "sha256", "bytes" })
                || ! exactString (object->getProperty ("preset_id"), result.presetId)
                || ! exactString (object->getProperty ("revision_id"), result.revisionId)
                || ! exactString (object->getProperty ("relative_path"), result.relativePath)
                || ! exactString (object->getProperty ("sha256"), result.sha256)
                || ! uuidV4 (result.presetId) || ! uuidV4 (result.revisionId)
                || ! sha256 (result.sha256)
                || result.relativePath != "reference/presets/" + result.presetId + "/"
                                             + result.revisionId + ".v1.json"
                || ! exactInteger (object->getProperty ("bytes"), 1,
                                   maximumSourcePresetBytes, result.bytes))
                return false;
            return true;
        }

        bool parsePresetReceipt (const juce::var& value, const juce::String& workId,
                                 RuntimePresetReceipt& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, {
                    "preset_id", "revision_id", "relative_path", "sha256", "bytes" })
                || ! exactString (object->getProperty ("preset_id"), result.presetId)
                || ! exactString (object->getProperty ("revision_id"), result.revisionId)
                || ! exactString (object->getProperty ("relative_path"), result.relativePath)
                || ! exactString (object->getProperty ("sha256"), result.sha256)
                || ! uuidV4 (result.presetId) || ! uuidV4 (result.revisionId)
                || ! sha256 (result.sha256)
                || result.relativePath != "plugin_data/reference/v2/presets/" + workId
                                              + "/" + result.presetId + ".json"
                || ! exactInteger (object->getProperty ("bytes"), 1,
                                   maximumPresetBytes, result.bytes))
                return false;
            return true;
        }

        bool parseManifest (const juce::var& value, const juce::String& expectedWorkId,
                            RuntimeManifest& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, {
                    "format", "version", "work_id", "revision", "source_state_artifact",
                    "active_preset", "preset_artifacts" })
                || object->getProperty ("format") != "kirin_hypha_reference_manifest"
                || object->getProperty ("version") != "2.0"
                || ! exactString (object->getProperty ("work_id"), result.workId)
                || ! workUuid (result.workId) || result.workId != expectedWorkId
                || ! exactInteger (object->getProperty ("revision"), 1,
                                   maximumSafeInteger, result.revision))
                return false;
            const auto stateValue = object->getProperty ("source_state_artifact");
            const auto* stateObject = stateValue.getDynamicObject();
            if (stateObject == nullptr || ! exactProperties (*stateObject, { "relative_path", "sha256", "bytes" })
                || ! exactString (stateObject->getProperty ("relative_path"), result.sourceStateArtifact.relativePath)
                || ! exactString (stateObject->getProperty ("sha256"), result.sourceStateArtifact.sha256)
                || ! sha256 (result.sourceStateArtifact.sha256)
                || result.sourceStateArtifact.relativePath != "reference/states/"
                       + result.sourceStateArtifact.sha256 + ".v1.json"
                || ! exactInteger (stateObject->getProperty ("bytes"), 1,
                                   maximumSourceStateBytes, result.sourceStateArtifact.bytes))
                return false;

            const auto* presets = object->getProperty ("preset_artifacts").getArray();
            if (presets == nullptr || presets->size() > 128)
                return false;
            std::set<std::string> presetIds, revisionIds, paths;
            for (const auto& item : *presets)
            {
                RuntimePresetReceipt receipt;
                if (! parsePresetReceipt (item, result.workId, receipt)
                    || ! presetIds.emplace (receipt.presetId.toStdString()).second
                    || ! revisionIds.emplace (receipt.revisionId.toStdString()).second
                    || ! paths.emplace (receipt.relativePath.toStdString()).second)
                    return false;
                result.presetArtifacts.push_back (std::move (receipt));
            }

            const auto active = object->getProperty ("active_preset");
            if (active.isVoid())
                return presets->isEmpty();
            const auto* activeObject = active.getDynamicObject();
            if (activeObject == nullptr || ! exactProperties (*activeObject, { "preset_id", "revision_id" })
                || ! exactString (activeObject->getProperty ("preset_id"), result.activePresetId)
                || ! exactString (activeObject->getProperty ("revision_id"), result.activePresetRevisionId)
                || ! uuidV4 (result.activePresetId) || ! uuidV4 (result.activePresetRevisionId))
                return false;
            for (const auto& receipt : result.presetArtifacts)
                if (receipt.presetId == result.activePresetId
                    && receipt.revisionId == result.activePresetRevisionId)
                    return true;
            return false;
        }

        bool parseSourceIdentity (const juce::var& value, const juce::String& kind,
                                  RuntimeCandidate& candidate)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr)
                return false;
            if (kind == "work_version")
            {
                juce::String work, recording, version, fileHash, pcmHash;
                if (! exactProperties (*object, { "work_id", "recording_id", "version_id", "sha256_file", "sha256_pcm" })
                    || ! exactString (object->getProperty ("work_id"), work)
                    || ! exactString (object->getProperty ("recording_id"), recording)
                    || ! exactString (object->getProperty ("version_id"), version)
                    || ! exactString (object->getProperty ("sha256_file"), fileHash)
                    || ! exactString (object->getProperty ("sha256_pcm"), pcmHash)
                    || ! uuidV4 (work) || ! uuidV4 (recording) || ! uuidV4 (version)
                    || ! sha256 (fileHash) || ! sha256 (pcmHash))
                    return false;
                candidate.sourceWorkId = work;
                candidate.sourceRecordingId = recording;
                candidate.sourceVersionId = version;
                candidate.sourceIdentityKey = work + ":" + recording + ":" + version
                    + ":" + fileHash + ":" + pcmHash;
                return true;
            }
            if (kind == "catalog_track")
            {
                juce::String catalog, fileHash, pcmHash;
                if (! exactProperties (*object, { "catalog_reference_id", "sha256_file", "sha256_pcm" })
                    || ! exactString (object->getProperty ("catalog_reference_id"), catalog)
                    || ! exactString (object->getProperty ("sha256_file"), fileHash)
                    || ! exactString (object->getProperty ("sha256_pcm"), pcmHash)
                    || ! matches (catalog, R"(^[a-z0-9][a-z0-9._:-]{0,127}$)")
                    || ! sha256 (fileHash) || ! sha256 (pcmHash))
                    return false;
                candidate.sourceIdentityKey = catalog + ":" + fileHash + ":" + pcmHash;
                return true;
            }
            return false;
        }

        bool parseCue (const juce::var& value, RuntimeCue& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, {
                    "cue_id", "label", "sample_rate_hz", "start_sample", "end_sample", "loop_enabled" })
                || ! exactString (object->getProperty ("cue_id"), result.cueId)
                || ! uuidV4 (result.cueId)
                || ! displayText (object->getProperty ("label"), 160, result.label)
                || ! exactInteger (object->getProperty ("sample_rate_hz"), 8'000, 768'000, result.sampleRateHz)
                || ! exactInteger (object->getProperty ("start_sample"), 0, maximumSafeInteger, result.startSample)
                || ! exactInteger (object->getProperty ("end_sample"), 1, maximumSafeInteger, result.endSample)
                || result.endSample <= result.startSample
                || ! object->getProperty ("loop_enabled").isBool())
                return false;
            result.loopEnabled = static_cast<bool> (object->getProperty ("loop_enabled"));
            return true;
        }

        bool parseCandidate (const juce::var& value, RuntimeCandidate& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, {
                    "candidate_id", "display_name", "source_kind", "source_identity",
                    "source_artifact", "cues", "default_cue_id" })
                || ! exactString (object->getProperty ("candidate_id"), result.candidateId)
                || ! uuidV4 (result.candidateId)
                || ! displayText (object->getProperty ("display_name"), 160, result.displayName)
                || ! exactString (object->getProperty ("source_kind"), result.sourceKind)
                || ! parseSourceIdentity (object->getProperty ("source_identity"), result.sourceKind,
                                         result)
                || ! parseContentReceipt (object->getProperty ("source_artifact"), "sources",
                                         64 * 1024, result.sourceArtifact)
                || ! exactString (object->getProperty ("default_cue_id"), result.defaultCueId)
                || ! uuidV4 (result.defaultCueId))
                return false;
            const auto* cues = object->getProperty ("cues").getArray();
            if (cues == nullptr || cues->isEmpty() || cues->size() > 4)
                return false;
            std::set<std::string> ids;
            bool foundDefault = false;
            for (const auto& item : *cues)
            {
                RuntimeCue cue;
                if (! parseCue (item, cue) || ! ids.emplace (cue.cueId.toStdString()).second)
                    return false;
                foundDefault = foundDefault || cue.cueId == result.defaultCueId;
                result.cues.push_back (std::move (cue));
            }
            return foundDefault;
        }

        bool parseProfileBindings (const juce::var& value,
                                   std::vector<RuntimeProfileBinding>& result)
        {
            const auto* bindings = value.getArray();
            if (bindings == nullptr || bindings->size() > 3)
                return false;
            std::set<std::string> paths;
            std::int64_t total = 0;
            for (const auto& item : *bindings)
            {
                const auto* object = item.getDynamicObject();
                RuntimeProfileBinding binding;
                if (object == nullptr || ! exactProperties (*object, { "profile_artifact", "weight_basis_points" })
                    || ! parseContentReceipt (object->getProperty ("profile_artifact"), "profiles",
                                             128 * 1024, binding.profileArtifact)
                    || ! exactInteger (object->getProperty ("weight_basis_points"), 1, 10'000,
                                      binding.weightBasisPoints)
                    || ! paths.emplace (binding.profileArtifact.relativePath.toStdString()).second)
                    return false;
                total += binding.weightBasisPoints;
                result.push_back (std::move (binding));
            }
            return result.empty() || total == 10'000;
        }

        bool parseCheck (const juce::var& value, RuntimeCheck& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, {
                    "check_id", "label", "mode", "view_bindings", "comparison_mode",
                    "candidates", "profile_bindings" })
                || ! exactString (object->getProperty ("check_id"), result.checkId)
                || ! uuidV4 (result.checkId)
                || ! displayText (object->getProperty ("label"), 80, result.label)
                || ! exactString (object->getProperty ("mode"), result.mode)
                || (result.mode != "audition_only" && result.mode != "audition_with_facts")
                || ! exactString (object->getProperty ("comparison_mode"), result.comparisonMode)
                || (result.comparisonMode != "original" && result.comparisonMode != "loudness_match"
                    && result.comparisonMode != "peak_match"))
                return false;
            const auto* views = object->getProperty ("view_bindings").getArray();
            const std::set<juce::String> validViews { "waveform", "spectrum_full", "spectrum_low",
                "loudness", "dynamics", "transient", "stereo" };
            std::set<std::string> viewNames;
            if (views == nullptr || views->size() > 3)
                return false;
            for (const auto& item : *views)
            {
                juce::String name;
                if (! exactString (item, name) || validViews.find (name) == validViews.end()
                    || ! viewNames.emplace (name.toStdString()).second)
                    return false;
                result.viewBindings.push_back (name);
            }
            if (result.mode == "audition_with_facts" && result.viewBindings.empty())
                return false;

            const auto* candidates = object->getProperty ("candidates").getArray();
            if (candidates == nullptr || candidates->isEmpty() || candidates->size() > 16)
                return false;
            std::set<std::string> candidateIds, identities;
            for (const auto& item : *candidates)
            {
                RuntimeCandidate candidate;
                if (! parseCandidate (item, candidate)
                    || ! candidateIds.emplace (candidate.candidateId.toStdString()).second
                    || ! identities.emplace ((candidate.sourceKind + ":" + candidate.sourceIdentityKey).toStdString()).second)
                    return false;
                result.candidates.push_back (std::move (candidate));
            }
            return parseProfileBindings (object->getProperty ("profile_bindings"),
                                         result.profileBindings);
        }

        bool parsePreset (const juce::var& value, const RuntimePresetReceipt& expected,
                          const juce::String& workId, RuntimePreset& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, {
                    "format", "version", "work_id", "source_preset_artifact", "name", "checks" })
                || object->getProperty ("format") != "kirin_hypha_reference_preset"
                || object->getProperty ("version") != "2.0"
                || ! exactString (object->getProperty ("work_id"), result.workId)
                || result.workId != workId
                || ! parseSourcePresetReceipt (object->getProperty ("source_preset_artifact"),
                                              result.sourcePresetArtifact)
                || result.sourcePresetArtifact.presetId != expected.presetId
                || result.sourcePresetArtifact.revisionId != expected.revisionId
                || ! displayText (object->getProperty ("name"), 80, result.name))
                return false;
            const auto* checks = object->getProperty ("checks").getArray();
            if (checks == nullptr || checks->size() > 64)
                return false;
            std::set<std::string> checkIds;
            for (const auto& item : *checks)
            {
                RuntimeCheck check;
                if (! parseCheck (item, check)
                    || ! checkIds.emplace (check.checkId.toStdString()).second)
                    return false;
                result.checks.push_back (std::move (check));
            }
            return true;
        }

        RuntimeWorkspaceLoadResult failure (juce::String code,
                                            std::shared_ptr<const RuntimeWorkspace> previous)
        {
            RuntimeWorkspaceLoadResult result;
            result.workspace = std::move (previous);
            result.state = result.workspace != nullptr
                ? RuntimeWorkspaceLoadState::retainedPrevious
                : RuntimeWorkspaceLoadState::rejected;
            result.rejectionCode = std::move (code);
            return result;
        }
    }

    RuntimeV2Repository::RuntimeV2Repository (juce::File transportRootIn)
        : root (std::move (transportRootIn))
    {
    }

    juce::File RuntimeV2Repository::transportRoot()
    {
       #if JUCE_WINDOWS
        auto local = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
        return local.getChildFile ("Kirin OS").getChildFile ("plugin_data")
                    .getChildFile ("reference").getChildFile ("v2");
       #else
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Library").getChildFile ("Application Support")
            .getChildFile ("Kirin OS").getChildFile ("plugin_data")
            .getChildFile ("reference").getChildFile ("v2");
       #endif
    }

    RuntimeWorkspaceLoadResult RuntimeV2Repository::refresh (
        const juce::String& workId,
        std::shared_ptr<const RuntimeWorkspace> previous) const
    {
        if (! workUuid (workId) || root == juce::File()
            || (previous != nullptr && previous->manifest.workId != workId))
            return failure ("reference_work_invalid", {});
        const auto manifestFile = root.getChildFile ("manifests").getChildFile (workId + ".json");
        if (! manifestFile.exists())
        {
            if (previous != nullptr)
                return failure ("reference_manifest_missing", std::move (previous));
            return {};
        }
        juce::MemoryBlock manifestBytes;
        juce::var manifestJson;
        RuntimeManifest manifest;
        if (! readJson (manifestFile, maximumManifestBytes, manifestBytes, manifestJson)
            || ! parseManifest (manifestJson, workId, manifest))
            return failure ("reference_manifest_rejected", std::move (previous));
        if (previous != nullptr)
        {
            if (manifest.revision < previous->manifest.revision)
                return failure ("reference_manifest_rollback", std::move (previous));
            if (manifest.revision == previous->manifest.revision)
                return { RuntimeWorkspaceLoadState::unchanged, std::move (previous), {} };
        }

        auto workspace = std::make_shared<RuntimeWorkspace>();
        workspace->manifest = manifest;
        for (const auto& receipt : manifest.presetArtifacts)
        {
            const auto file = root.getChildFile ("presets").getChildFile (workId)
                                  .getChildFile (receipt.presetId + ".json");
            juce::MemoryBlock bytes;
            juce::var json;
            if (! readJson (file, maximumPresetBytes, bytes, json)
                || bytes.getSize() != static_cast<size_t> (receipt.bytes)
                || juce::SHA256 (bytes).toHexString() != receipt.sha256)
                return failure ("reference_preset_receipt_rejected", std::move (previous));
            RuntimePreset preset;
            if (! parsePreset (json, receipt, workId, preset))
                return failure ("reference_preset_contract_rejected", std::move (previous));
            workspace->presets.push_back (std::move (preset));
        }
        return { RuntimeWorkspaceLoadState::updated,
                 std::shared_ptr<const RuntimeWorkspace> (std::move (workspace)), {} };
    }
}
