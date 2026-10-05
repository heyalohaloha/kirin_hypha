#pragma once

#include <cstdint>
#include <memory>
#include <limits>
#include <map>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "ReferenceCuePart.h"

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
        bool songEntry = false;  // B（REF）の曲。sets.json の B セットの曲 1 つを、1 Check・1 曲の Preset にしたもの
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

    // library/sets.json（Kirin OS が書く）。Kirin OS で「Hypha に出す」順位を付けた B セット
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

    // B の一覧と Balance に出す、曲の既定の Cue の Kirin OS の値（ranges）。無ければ NaN・空。
    struct RuntimeSongFacts
    {
        double lufsI = std::numeric_limits<double>::quiet_NaN();
        double maxTruePeak = std::numeric_limits<double>::quiet_NaN();
        std::vector<double> spectrumCentersHz;
        std::vector<float> spectrumMedianDb;
        // 既定の Cue が曲のどの部分か（図の凡例「Bサビ 1:02-1:24」）と、その時刻（秒）。
        CuePart part = CuePart::unknown;
        double partStartSeconds = std::numeric_limits<double>::quiet_NaN();
        double partEndSeconds = std::numeric_limits<double>::quiet_NaN();
    };

    // Kirin OS の準備の状態（library/preparation.json）。Hypha に出したセットの曲ごと。値は Kirin OS の
    // 言葉のまま（state：ready / playable / pending、step：queued / resolving / measuring、reason：
    // source_unavailable / analysis_failed、retry：automatic / manual。無ければ空）。phase は全体の動き。
    struct RuntimeSongPreparation
    {
        juce::String state, step, reason, retry, phase;
        int ahead = 0;  // この曲より先に準備する、まだ済んでいない曲の数
        bool known() const noexcept { return state.isNotEmpty(); }
    };

    struct RuntimeLibraryPreparation
    {
        juce::String phase;
        std::map<juce::String, RuntimeSongPreparation> songs;  // candidate_id ごと
        RuntimeSongPreparation find (const juce::String& candidateId) const
        {
            const auto found = songs.find (candidateId);
            return found != songs.end() ? found->second : RuntimeSongPreparation {};
        }
    };

    // Kirin OS の項目のうち、Hypha が受け付けずに外したもの（ほかの項目は使う。2026-10-06：1 つの名前のせいで
    // ライブラリ全体を捨て、違う直し方を言っていた）。B セットの曲・Version・Check の候補曲・B セット。
    struct RuntimeSkippedItem
    {
        juce::String name;           // 画面に出せる形にした名前（読めなければ空）
        bool nameUnreadable = false; // 名前（曲名・Cue の名前）に Hypha が受け付けない字がある（Kirin OS で名前を直す）
        bool operator== (const RuntimeSkippedItem& other) const noexcept
        { return name == other.name && nameUnreadable == other.nameUnreadable; }
    };

    struct RuntimeLibrarySets
    {
        std::int64_t revision = 0;
        juce::String hash;
        std::vector<RuntimeSongSet> songSets;
        std::map<juce::String, RuntimeSongFacts> songFacts; // 曲の Preset の ID ごと
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
        // sets.json が無い・読めないときは空（manifest だけで今までどおり動く）。新しい manifest に sets.json が
        // まだ追いついていない・読めないときは、前の sets を保つ（CHECK セットは今の manifest にある Preset だけ）。
        std::optional<RuntimeLibrarySets> librarySets;
        juce::String librarySetsIssue; // sets.json を読めなかった・一部を飛ばした理由（B の画面が直し方を出す）
        std::vector<RuntimeSkippedItem> librarySkipped, setsSkipped; // manifest・sets.json から外した項目
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
