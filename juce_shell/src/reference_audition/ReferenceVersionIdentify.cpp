#include "ReferenceVersionIdentify.h"
#include "ReferenceSourceRanges.h"

#include <algorithm>
#include <set>

namespace hypha::reference_audition
{
namespace
{
constexpr std::int64_t longestSongTicks = 36'000;  // 1 時間。これより後ろの位置では照合しない（DAW のずっと先）
}

void VersionIdentifier::prepare (const juce::File& root, const RuntimeWorkspace& workspace)
{
    const auto key = workspace.publicationHash + ":" + (workspace.librarySets ? workspace.librarySets->hash : juce::String {});
    if (key == preparedKey) return;
    preparedKey = key;
    candidates.clear();
    if (! workspace.library || ! workspace.librarySets) return;
    // 画面の Version の選択肢と同じ並び・同じ ID（Preset/Check/候補）。同じ音源は 1 回だけ。
    std::set<juce::String> seen;
    for (const auto& preset : workspace.presets)
    {
        if (preset.songEntry || (workspace.independentVersions && ! preset.versionEntry)) continue;
        for (const auto& check : preset.checks)
            for (const auto& candidate : check.candidates)
            {
                if (candidate.sourceKind != "work_version" || ! seen.insert (candidate.sourceIdentityKey).second) continue;
                const RuntimeSourceRangesReceipt* receipt = nullptr;
                for (const auto& entry : workspace.librarySets->sourceRanges)
                    if (entry.sourceArtifactSha256 == candidate.sourceArtifact.sha256) receipt = &entry;
                if (receipt == nullptr) continue;
                auto cached = printsByRanges.find (receipt->rangesArtifact.sha256);
                if (cached == printsByRanges.end())
                {
                    RuntimeSourceRanges ranges;
                    KirinFingerprint print;
                    if (readReferenceSourceRanges (root, receipt->rangesArtifact, ranges) && ranges.fingerprintTicks > 0)
                        print = decodeFingerprint (ranges.fingerprintChromaSigns, ranges.fingerprintLoudness, ranges.fingerprintTicks);
                    cached = printsByRanges.emplace (receipt->rangesArtifact.sha256, std::move (print)).first;
                }
                if (! cached->second.bits.empty())
                    candidates.emplace_back (preset.sourcePresetArtifact.presetId + "/" + check.checkId + "/" + candidate.candidateId,
                                             cached->second);
            }
    }
}

void VersionIdentifier::setCandidates (std::vector<std::pair<juce::String, KirinFingerprint>> next)
{
    candidates = std::move (next);
    preparedKey.clear();
}

VersionIdentity VersionIdentifier::identify (const KirinFingerprint& slice, std::int64_t endTick) const
{
    VersionIdentity result;
    const auto count = static_cast<std::int64_t> (slice.bits.size());
    if (count == 0 || endTick < 0 || endTick >= longestSongTicks || slice.lufs.size() != slice.bits.size()) return result;
    // 曲の頭からの位置に置いた A（鳴っていない所は −70 LUFS）。照合は鳴っている所だけを回す。
    KirinFingerprint placed;
    placed.bits.assign (static_cast<size_t> (endTick + 1), 0);
    placed.lufs.assign (static_cast<size_t> (endTick + 1), -70.0f);
    for (std::int64_t index = 0; index < count; ++index)
        if (const auto at = endTick - (count - 1) + index; at >= 0)
        {
            placed.bits[static_cast<size_t> (at)] = slice.bits[static_cast<size_t> (index)];
            placed.lufs[static_cast<size_t> (at)] = slice.lufs[static_cast<size_t> (index)];
        }
    for (const auto& [id, print] : candidates)
    {
        const auto match = compareFingerprints (placed, print);
        if (match.relation != FingerprintMatch::Relation::unknown) result.matches.push_back ({ id, match.agreement, match.relation });
    }
    std::stable_sort (result.matches.begin(), result.matches.end(),
                      [] (const auto& left, const auto& right) { return left.agreement > right.agreement; });
    for (const auto& match : result.matches)
        if (match.relation == FingerprintMatch::Relation::sameSong || match.relation == FingerprintMatch::Relation::nearIdentical)
        {
            result.autoId = match.versionId;
            result.autoAgreement = match.agreement;
            break;
        }
    return result;
}
}
