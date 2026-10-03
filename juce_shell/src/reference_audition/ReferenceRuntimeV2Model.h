#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

namespace hypha::reference_audition
{
    struct RuntimeContentReceipt
    {
        juce::String relativePath;
        juce::String sha256;
        std::int64_t bytes = 0;
    };

    struct RuntimeSourcePresetReceipt
    {
        juce::String presetId;
        juce::String revisionId;
        juce::String relativePath;
        juce::String sha256;
        std::int64_t bytes = 0;
    };

    struct RuntimePresetReceipt : RuntimeSourcePresetReceipt {};

    struct RuntimeGlobalPresetCatalogEntry
    {
        juce::String presetId;
        juce::String revisionId;
        juce::String nameSnapshot;
        juce::String origin;
    };

    struct RuntimeGlobalPresetCatalog
    {
        std::vector<RuntimeGlobalPresetCatalogEntry> presets;
    };

    struct RuntimeCue
    {
        juce::String cueId;
        juce::String label;
        std::int64_t sampleRateHz = 0;
        std::int64_t startSample = 0;
        std::int64_t endSample = 0;
        bool loopEnabled = false;
    };

    struct RuntimeCandidate
    {
        juce::String candidateId;
        juce::String displayName;
        juce::String sourceKind;
        juce::String sourceIdentityKey;
        juce::String sourceWorkId;
        juce::String sourceRecordingId;
        juce::String sourceVersionId;
        RuntimeContentReceipt sourceArtifact;
        std::vector<RuntimeCue> cues;
        juce::String defaultCueId;
        bool prepared = true;
    };

    struct RuntimeProfileBinding
    {
        RuntimeContentReceipt profileArtifact;
        std::int64_t weightBasisPoints = 0;
    };

    struct RuntimeCheck
    {
        juce::String checkId;
        juce::String label;
        juce::String mode;
        std::vector<juce::String> viewBindings;
        juce::String comparisonMode;
        std::vector<RuntimeCandidate> candidates;
        std::vector<RuntimeProfileBinding> profileBindings;
    };

    struct RuntimePreset
    {
        bool versionEntry = false;
        bool songEntry = false;  // H8: B（REF）の曲。sets.json の B セットの曲 1 つを、1 Check・1 曲の Preset にしたもの
        juce::String workId;
        RuntimeSourcePresetReceipt sourceTemplateArtifact;
        RuntimeSourcePresetReceipt sourcePresetArtifact;
        juce::String name;
        std::vector<RuntimeCheck> checks;
    };

    struct RuntimePendingPreset
    {
        RuntimeSourcePresetReceipt sourceTemplateArtifact;
        RuntimeSourcePresetReceipt sourcePresetArtifact;
        juce::String name;
    };

    struct RuntimeManifest
    {
        juce::String workId;
        std::int64_t revision = 0;
        RuntimeContentReceipt sourceStateArtifact;
        RuntimeContentReceipt globalPresetCatalogArtifact;
        juce::String activePresetId;
        juce::String activePresetRevisionId;
        std::vector<RuntimePresetReceipt> presetArtifacts;
        std::vector<RuntimePendingPreset> pendingPresets;
    };

    // H1: library/sets.json（Kirin OS の K2・K3）。Kirin OS で「Hypha に出す」順位を付けた B セット
    // （参照曲の並び）と CHECK セット（Preset）、それらの曲の Cue の値のファイル（ranges/<sha256>.json）の索引。
    struct RuntimeSongSet
    {
        juce::String songSetId;
        juce::String revisionId;
        juce::String name;
        int rank = 0;
        std::vector<RuntimeCandidate> songs;
    };

    struct RuntimeCheckSetRank
    {
        juce::String presetId;
        juce::String revisionId;
        int rank = 0;
    };

    struct RuntimeSourceRangesReceipt
    {
        juce::String sourceArtifactSha256;
        RuntimeContentReceipt rangesArtifact;
    };

    struct RuntimeLibrarySets
    {
        std::int64_t revision = 0;
        juce::String hash;
        std::vector<RuntimeSongSet> songSets;
        std::vector<RuntimeCheckSetRank> checkSets;
        std::vector<RuntimeSourceRangesReceipt> sourceRanges;
    };

    struct RuntimeWorkspace
    {
        bool library = false;
        bool independentVersions = false;
        juce::String publicationHash;
        RuntimeManifest manifest;
        RuntimeGlobalPresetCatalog globalPresetCatalog;
        std::vector<RuntimePreset> presets;
        // sets.json が無い・まだ別の manifest のもの・読めないときは空（manifest だけで今までどおり動く）。
        std::optional<RuntimeLibrarySets> librarySets;
    };

    enum class RuntimeWorkspaceLoadState
    {
        missing,
        unchanged,
        updated,
        retainedPrevious,
        rejected,
    };

    struct RuntimeWorkspaceLoadResult
    {
        RuntimeWorkspaceLoadState state = RuntimeWorkspaceLoadState::missing;
        std::shared_ptr<const RuntimeWorkspace> workspace;
        juce::String rejectionCode;

        bool usable() const noexcept { return workspace != nullptr; }
    };
}
