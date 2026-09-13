#include "ReferenceRuntimeRepositoryParsing.h"
#include "ReferenceRuntimeV2PresetParsing.h"
#include "ReferenceRuntimePendingPresets.h"

#include <algorithm>
#include <limits>
#include <regex>
#include <set>
#include <utility>

#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
    namespace runtime_repository_parsing
    {
        constexpr std::int64_t maximumSourceStateBytes = 1024 * 1024;
        constexpr std::int64_t maximumSafeInteger = 9'007'199'254'740'991;

        static bool matches (const juce::String& value, const char* expression)
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

        static bool exactString (const juce::var& value, juce::String& result)
        {
            if (! value.isString())
                return false;
            result = value.toString();
            return true;
        }

        static bool displayText (const juce::var& value, int maximumCharacters,
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

        static bool parseContentReceipt (const juce::var& value, const juce::String& kind,
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

        bool parseManifest (const juce::var& value, const juce::String& expectedWorkId,
                            RuntimeManifest& result)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! runtimeManifestKeys (*object)
                || object->getProperty ("format") != "kirin_hypha_reference_manifest"
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
            if (! parseContentReceipt (
                    object->getProperty ("global_preset_catalog_artifact"),
                    "global_preset_catalogs", maximumGlobalPresetCatalogBytes,
                    result.globalPresetCatalogArtifact))
                return false;

            const auto* presets = object->getProperty ("preset_artifacts").getArray();
            if (presets == nullptr || presets->size() > 128)
                return false;
            std::set<std::string> presetIds, revisionIds, paths;
            for (const auto& item : *presets)
            {
                RuntimePresetReceipt receipt;
                if (! runtime_v2_parsing::parsePresetReceipt (item, result.workId, receipt)
                    || ! presetIds.emplace (receipt.presetId.toStdString()).second
                    || ! revisionIds.emplace (receipt.revisionId.toStdString()).second
                    || ! paths.emplace (receipt.relativePath.toStdString()).second)
                    return false;
                result.presetArtifacts.push_back (std::move (receipt));
            }

            if (! parseRuntimePendingPresets (*object, result)) return false;
            const auto active = object->getProperty ("active_preset");
            if (active.isVoid())
                return presets->isEmpty() && result.pendingPresets.empty();
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
            for (const auto& pending : result.pendingPresets)
                if (pending.sourcePresetArtifact.presetId == result.activePresetId
                    && pending.sourcePresetArtifact.revisionId == result.activePresetRevisionId) return true;
            return false;
        }

        static bool parseSourceIdentity (const juce::var& value, const juce::String& kind,
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

        static bool parseCue (const juce::var& value, RuntimeCue& result)
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

        static bool parseCandidate (const juce::var& value, RuntimeCandidate& result,
                             bool progressive)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, progressive
                    ? std::initializer_list<const char*> { "candidate_id", "display_name", "source_kind", "source_identity",
                        "source_artifact", "cues", "default_cue_id", "preparation_status" }
                    : std::initializer_list<const char*> { "candidate_id", "display_name", "source_kind", "source_identity",
                        "source_artifact", "cues", "default_cue_id" })
                || ! exactString (object->getProperty ("candidate_id"), result.candidateId)
                || ! uuidV4 (result.candidateId)
                || ! displayText (object->getProperty ("display_name"), 160, result.displayName)
                || ! exactString (object->getProperty ("source_kind"), result.sourceKind)
                || ! parseSourceIdentity (object->getProperty ("source_identity"), result.sourceKind,
                                         result)
                || ! exactString (object->getProperty ("default_cue_id"), result.defaultCueId)
                || ! uuidV4 (result.defaultCueId))
                return false;
            const auto status = object->getProperty ("preparation_status");
            result.prepared = ! progressive || status == "prepared";
            if (progressive && status != "prepared" && status != "pending") return false;
            if (result.prepared != ! object->getProperty ("source_artifact").isVoid()
                || (result.prepared && ! parseContentReceipt (object->getProperty ("source_artifact"), "sources",
                                                              64 * 1024, result.sourceArtifact))) return false;
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

        static bool parseProfileBindings (const juce::var& value,
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

        static bool parseCheck (const juce::var& value, RuntimeCheck& result, bool progressive, bool library)
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
            if (candidates == nullptr || (! library && candidates->isEmpty()) || candidates->size() > 16)
                return false;
            std::set<std::string> candidateIds, identities;
            for (const auto& item : *candidates)
            {
                RuntimeCandidate candidate;
                if (! parseCandidate (item, candidate, progressive)
                    || ! candidateIds.emplace (candidate.candidateId.toStdString()).second
                    || ! identities.emplace ((candidate.sourceKind + ":" + candidate.sourceIdentityKey).toStdString()).second)
                    return false;
                result.candidates.push_back (std::move (candidate));
            }
            return (library || std::any_of (result.candidates.begin(), result.candidates.end(),
                                [] (const auto& candidate) { return candidate.prepared; }))
                && parseProfileBindings (object->getProperty ("profile_bindings"),
                                         result.profileBindings);
        }

        bool parsePreset (const juce::var& value, const RuntimePresetReceipt& expected,
                          const juce::String& workId, RuntimePreset& result, bool library)
        {
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, library
                    ? std::initializer_list<const char*> { "format", "version", "source_template_artifact", "name", "checks" }
                    : std::initializer_list<const char*> { "format", "version", "work_id", "source_template_artifact",
                    "source_preset_artifact", "name", "checks" })
                || object->getProperty ("format") != (library ? "kirin_hypha_reference_library_preset" : "kirin_hypha_reference_preset")
                || (library ? object->getProperty ("version") != "1.0"
                    : (object->getProperty ("version") != "2.0" && object->getProperty ("version") != "3.0"))
                || (! library && ! exactString (object->getProperty ("work_id"), result.workId))
                || result.workId != workId
                || ! runtime_v2_parsing::parseSourcePresetReceipt (
                    object->getProperty ("source_template_artifact"),
                    result.sourceTemplateArtifact)
                || ! runtime_v2_parsing::parseSourcePresetReceipt (
                    object->getProperty (library ? "source_template_artifact" : "source_preset_artifact"),
                    result.sourcePresetArtifact)
                || result.sourceTemplateArtifact.presetId
                       != result.sourcePresetArtifact.presetId
                || (! library && result.sourceTemplateArtifact.revisionId
                       == result.sourcePresetArtifact.revisionId)
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
                if (! parseCheck (item, check, library || object->getProperty ("version") == "3.0", library)
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

}
