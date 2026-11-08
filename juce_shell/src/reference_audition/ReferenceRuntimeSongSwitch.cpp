#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceSourceRevisionService.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <system_error>
#include <utility>

// B の曲の切替。旧曲を鳴らしたまま新しい曲を準備し、新しい PCM・位置・MATCH の gain を一組で Audio Thread へ渡す。
// A へは戻らない。一組がそろわないあいだは旧曲を保ち、最後の押下を持ったまま自動で完了する。
namespace hypha::reference_audition
{
namespace
{
constexpr std::int64_t matchRetryMs = 100, preloadRetryMs = 5000, preloadScanMs = 1000;
constexpr std::int64_t switchRetireLimitMs = 500;  // 音声 callback が止まっていても前の面を持ち続けない
const RuntimeCandidate* findSong (const RuntimeWorkspace& workspace, const juce::String& id)
{
    const auto parts = juce::StringArray::fromTokens (id, "/", {});
    if (parts.size() != 3) return nullptr;
    for (const auto& preset : workspace.presets)
        if (preset.songEntry && preset.sourcePresetArtifact.presetId == parts[0])
            for (const auto& check : preset.checks)
                if (check.checkId == parts[1])
                    for (const auto& candidate : check.candidates)
                        if (candidate.candidateId == parts[2]) return &candidate;
    return nullptr;
}
}

// 鳴っている B を新しい曲へ替える。notAudible：鳴っていないので今までの公開の道へ。waiting：旧曲を保って待つ。
RuntimeV2Controller::SongSwitch RuntimeV2Controller::switchAudibleSong (WorkspaceSelection& prepared, AudioPages& staging)
{
    const auto audible = [this]() noexcept
    {
        return pairedSongSwitch.load (std::memory_order_acquire) && songsOnly.load (std::memory_order_acquire)
            && bSelected.load (std::memory_order_acquire) && ready.load (std::memory_order_acquire)
            && latestPlaying.load (std::memory_order_acquire) && latestPositionValid.load (std::memory_order_acquire)
            && ! revokeAfterFade.load (std::memory_order_acquire) && ! blind.ongoing();
    };
    if (! audible()) return SongSwitch::notAudible;
    const auto nowMs = juce::Time::currentTimeMillis();
    auto& next = prepared.next;
    next.sourceLoudnessOffsetLu = staging.playbackLoudnessOffsetLu();
    const auto levels = matchLevels (*prepared.source, next.cueLevelAvailable, next.cueIntegratedLoudness,
                                     next.cueMaximumTruePeakDbtp, next.sourceLoudnessOffsetLu);
    LiveLevelReading level;
    {
        // 押したときの A で合わせる。読めなければ旧曲を合わせていた A（追従がこの後すぐ直す）。
        const juce::ScopedLock lock (stateLock);
        level = latestLiveLevel;
        if (! std::isfinite (level.loudness)) level.loudness = currentSnapshot.aIntegratedLoudness;
        if (! std::isfinite (level.peak)) level.peak = currentSnapshot.aMaximumTruePeakDbtp;
    }
    double required = 0.0;
    PreparedNormalSelection match;
    auto failure = requiredMatchGain (next.comparisonMode, levels, level.loudness, level.peak, required);
    const bool knownMode = next.comparisonMode == "loudness_match" || next.comparisonMode == "peak_match" || next.comparisonMode == "original";
    const bool matched = knownMode && failure == MatchFailure::none
        && matchedSelection (next.comparisonMode, required, levels, level.loudness, level.peak, match, failure);
    if (! matched)
    {
        // 新しい gain が無い PCM は渡さない。確かめ済みの旧曲を保ち、値が届いたら自動で完了する。
        holdSelectionForSourceRetry (failure == MatchFailure::liveLevelUnavailable
            ? "reference_source_revalidating" : "reference_source_level_unavailable", failure != MatchFailure::liveLevelUnavailable);
        prepared.matchRetryAtMs = nowMs + matchRetryMs;
        return SongSwitch::waiting;
    }
    const juce::ScopedLock lock (stateLock);
    if (requestedSelection.generation != prepared.selection.generation) return SongSwitch::waiting;
    if (! audible()) return SongSwitch::notAudible;
    pendingSourceFailureSinceMs = 0; pendingSourceFailureReason.clear();
    match.auditionEpoch = auditionEpoch.load (std::memory_order_acquire);
    match.selectionGeneration = prepared.selection.generation;
    auto completed = std::move (prepared);
    clearPreparedWorkspace();
    finishWorkspaceSelection (std::move (completed), &match);
    return SongSwitch::switched;
}

// 前の面を閉じてよいのは、Audio Thread が読み終えた・鳴らしていない・期限が切れたとき。閉じるまで次の準備に使わない。
bool RuntimeV2Controller::serviceSwitchRetirement()
{
    if (songBankRetiringSinceMs == 0) return false;
    const bool finished = normalSwitchRetired.load (std::memory_order_acquire) == normalSwitchSerial.load (std::memory_order_acquire);
    const bool silent = ! normalAudible.load (std::memory_order_acquire) || ! latestPlaying.load (std::memory_order_acquire);
    if (! finished && ! silent && juce::Time::currentTimeMillis() - songBankRetiringSinceMs < switchRetireLimitMs) return true;
    nextPages().close();
    songBankRetiringSinceMs = 0;
    return false;
}

void RuntimeV2Controller::preloadSongs (std::vector<juce::String> ids)
{
    {
        const juce::ScopedLock lock (stateLock);
        songPreloadWanted = std::move (ids);
    }
    notify();
}

void RuntimeV2Controller::clearSongPreload()
{
    if (songPreload.check.valid()) retiredSourceVerifications.push_back (std::move (songPreload.check));
    songPreload = {};
    const juce::ScopedLock lock (stateLock);
    songPreloadReady.clear();
}

// 作業スレッド。押した曲の確かめが先で、先読みは空いているときに 1 曲ずつ進める。
void RuntimeV2Controller::serviceSongPreload()
{
    auto& preload = songPreload;
    const auto nowMs = juce::Time::currentTimeMillis();
    if (preload.check.valid())
    {
        if (preload.check.wait_for (std::chrono::seconds (0)) == std::future_status::timeout) return;
        juce::String failure;
        try { failure = preload.check.get(); } catch (...) { failure = "reference_source_revalidating"; }
        if (failure.isEmpty())
        {
            preload.held[preload.key] = preload.source;
            preload.heldBytes += preload.source->revision.sizeBytes.getLargeIntValue();
        }
        else preload.retryAtMs[preload.key] = nowMs + preloadRetryMs;
        preload.source.reset(); preload.nextScanMs = 0;
    }
    if (nowMs < preload.nextScanMs || workspace == nullptr || preparedWorkspace || sourceVerification.check.valid()
        || appliedSelectionGeneration.load (std::memory_order_acquire) != requestedGeneration()) return;
    std::vector<juce::String> wanted;
    { const juce::ScopedLock lock (stateLock); wanted = songPreloadWanted; }
    startSongPreload (wanted, nowMs);
}

void RuntimeV2Controller::startSongPreload (const std::vector<juce::String>& wanted, std::int64_t nowMs)
{
    auto& preload = songPreload;
    preload.nextScanMs = nowMs + preloadScanMs;
    if (wanted.empty()) return;  // 別の画面の間は今ある先読みを保つ
    std::set<juce::String> keys;
    std::vector<const RuntimeCandidate*> songs;
    std::vector<juce::String> readyIds;
    for (const auto& id : wanted)
        if (const auto* song = findSong (*workspace, id); song != nullptr && song->prepared && songs.size() < preloadSongLimit)
        {
            keys.insert (song->sourceArtifact.sha256); songs.push_back (song);
            if (preload.held.count (song->sourceArtifact.sha256) != 0) readyIds.push_back (id);
        }
    { const juce::ScopedLock lock (stateLock); songPreloadReady = std::move (readyIds); }
    for (auto entry = preload.held.begin(); entry != preload.held.end();)
    {
        if (keys.count (entry->first) != 0) { ++entry; continue; }
        preload.heldBytes -= entry->second->revision.sizeBytes.getLargeIntValue();
        entry = preload.held.erase (entry);
    }
    for (const auto* song : songs)
    {
        const auto& key = song->sourceArtifact.sha256;
        const auto retry = preload.retryAtMs.find (key);
        if (preload.held.count (key) != 0 || (retry != preload.retryAtMs.end() && nowMs < retry->second)) continue;
        const auto loaded = sourceRepository.load (*song);
        if (! loaded.accepted()) { preload.retryAtMs[key] = nowMs + preloadRetryMs; continue; }
        if (preload.heldBytes + loaded.source->revision.sizeBytes.getLargeIntValue() > preloadByteLimit)
        { preload.retryAtMs[key] = nowMs + preloadRetryMs; continue; }
        try
        {
            preload.source = loaded.source; preload.key = key;
            preload.check = std::async (stagedSearchLaunch, [repository = sourceRepository, source = preload.source]
                { return repository.verifySourceFile (*source, false); });
        }
        catch (const std::system_error&) { preload.source.reset(); preload.retryAtMs[key] = nowMs + preloadRetryMs; }
        return;
    }
}
}
