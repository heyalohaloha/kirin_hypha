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

void VersionIdentifier::prepare (const juce::File& root, const RuntimeWorkspace& workspace, std::int64_t nowMs)
{
    const auto key = workspace.publicationHash + ":" + (workspace.librarySets ? workspace.librarySets->hash : juce::String {});
    if (key == preparedKey && (retryAtMs == 0 || nowMs < retryAtMs)) return;
    preparedKey = key;
    retryAtMs = 0;
    Candidates next;
    std::set<juce::String> used;
    if (workspace.library && workspace.librarySets)
    {
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
                    const auto& sha = receipt->rangesArtifact.sha256;
                    used.insert (sha);
                    auto cached = printsByRanges.find (sha);
                    if (cached == printsByRanges.end())
                    {
                        RuntimeSourceRanges ranges;
                        if (! readReferenceSourceRanges (root, receipt->rangesArtifact, ranges))
                        {
                            retryAtMs = nowMs + retryMs;  // 覚えない（書き終えたら読める）
                            continue;
                        }
                        KirinFingerprint print;
                        if (ranges.fingerprintTicks > 0)
                            print = decodeFingerprint (ranges.fingerprintChromaSigns, ranges.fingerprintLoudness, ranges.fingerprintTicks);
                        cached = printsByRanges.emplace (sha, std::move (print)).first;
                    }
                    if (! cached->second.bits.empty())
                        next.emplace_back (preset.sourcePresetArtifact.presetId + "/" + check.checkId + "/" + candidate.candidateId,
                                           cached->second);
                }
        }
    }
    for (auto it = printsByRanges.begin(); it != printsByRanges.end();)
        it = used.count (it->first) != 0 ? std::next (it) : printsByRanges.erase (it);
    auto published = std::make_shared<const Candidates> (std::move (next));
    const juce::ScopedLock guard (lock);
    candidates = std::move (published);
}

void VersionIdentifier::setCandidates (std::vector<std::pair<juce::String, KirinFingerprint>> next)
{
    auto published = std::make_shared<const Candidates> (std::move (next));
    const juce::ScopedLock guard (lock);
    candidates = std::move (published);
    preparedKey.clear();
}

std::shared_ptr<const VersionIdentifier::Candidates> VersionIdentifier::current() const
{
    const juce::ScopedLock guard (lock);
    return candidates;
}

size_t VersionIdentifier::candidateCount() const
{
    return current()->size();
}

bool autoEligible (const FingerprintMatch& match, bool anywhere) noexcept
{
    if (match.agreement >= strongSameSongAgreement && match.loudnessCorrelation >= autoMinimumLoudnessCorrelation) return true;
    return ! anywhere && match.agreement >= 0.62 && match.loudnessCorrelation >= 0.5;  // Kirin OS の弱い「同じ曲」
}

VersionIdentity VersionIdentifier::identify (const KirinFingerprint& slice, std::int64_t endTick) const
{
    VersionIdentity result;
    const auto count = static_cast<std::int64_t> (slice.bits.size());
    if (count == 0 || slice.lufs.size() != slice.bits.size()) return result;
    const auto prints = current();
    using Relation = FingerprintMatch::Relation;
    // 1. DAW の位置で：曲の頭からの位置に置いた A（鳴っていない所は −70 LUFS）を、Kirin OS と同じ ±30 秒で照合する。
    if (endTick >= 0 && endTick < longestSongTicks)
    {
        KirinFingerprint placed;
        placed.bits.assign (static_cast<size_t> (endTick + 1), 0);
        placed.lufs.assign (static_cast<size_t> (endTick + 1), -70.0f);
        for (std::int64_t index = 0; index < count; ++index)
            if (const auto at = endTick - (count - 1) + index; at >= 0)
            {
                placed.bits[static_cast<size_t> (at)] = slice.bits[static_cast<size_t> (index)];
                placed.lufs[static_cast<size_t> (at)] = slice.lufs[static_cast<size_t> (index)];
            }
        for (const auto& [id, print] : *prints)
        {
            const auto match = compareFingerprints (placed, print);
            if (match.relation != Relation::unknown)
                result.matches.push_back ({ id, match.agreement, match.relation, match.loudnessCorrelation, false, autoEligible (match, false) });
        }
    }
    // 2. どれも AUTO に届かなければ、曲が DAW の時間軸のどこにあっても（アルバムの 2 曲目など）探す。位置の手がかりが
    //    無いぶん、弱い「同じ曲」（一致率 0.62〜0.70）は採らず、一致率 0.70 以上だけを同じ曲とする。
    if (std::none_of (result.matches.begin(), result.matches.end(), [] (const auto& match) { return match.eligible; }))
    {
        result.matches.clear();
        for (const auto& [id, print] : *prints)
        {
            auto match = compareFingerprints (slice, print, static_cast<int> (1 - count), static_cast<int> (print.bits.size()) - 1);
            if (match.relation == Relation::sameSong && match.agreement < strongSameSongAgreement) match.relation = Relation::different;
            if (match.relation != Relation::unknown)
                result.matches.push_back ({ id, match.agreement, match.relation, match.loudnessCorrelation, true, autoEligible (match, true) });
        }
    }
    std::stable_sort (result.matches.begin(), result.matches.end(),
                      [] (const auto& left, const auto& right) { return left.agreement > right.agreement; });
    // AUTO は AUTO にできるもので一致率の最も高い Version（作業中のミックスと前の書き出しは同じ曲になる）。
    for (const auto& match : result.matches)
        if (match.eligible)
        {
            result.autoId = match.versionId;
            result.autoAgreement = match.agreement;
            break;
        }
    return result;
}

juce::String AutoVersionChooser::next (const VersionIdentity& identity, const juce::String& currentId, bool currentIsAuto)
{
    const auto reset = [this] { candidate.clear(); streak = 0; return juce::String {}; };
    if (identity.autoId.isEmpty() || identity.autoId == currentId) return reset();
    if (currentId.isNotEmpty() && ! currentIsAuto) return reset();  // 利用者が選んだ Version は替えない
    for (const auto& match : identity.matches)  // AUTO の選んだものがまだ合っていて、差が小さければ替えない
        if (match.versionId == currentId && match.eligible && identity.autoAgreement < match.agreement + switchMargin)
            return reset();
    streak = candidate == identity.autoId ? streak + 1 : 1;
    candidate = identity.autoId;
    return streak >= confirmations ? identity.autoId : juce::String {};
}
}
