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

#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
void observeReferenceFollowWrite (const RuntimeV2Controller*);
#endif

// 流し方・ずらしの写像の書き換え。曲の切替の公開と同じ書き方：normalSwitchWriting を立てて奇数の間に書き（書いている間の
// block は前の写像を鳴らし切る）、switchSerial を進めて鳴っている役を前の写像から新しい写像へクロスフェードさせる。gain は
// 新しい写像と一組で書く（前の gain が新しい写像で鳴らない）。mappingWriteLock を持って呼ぶ。
void RuntimeV2Controller::writeFollowMappingLocked (bool follow, std::int64_t offset, std::optional<float> gain) noexcept
{
    normalSwitchWriting.store (true, std::memory_order_release);
    mappingGeneration.fetch_add (1, std::memory_order_acq_rel);
   #if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    observeReferenceFollowWrite (this);
   #endif
    if (followA.load (std::memory_order_relaxed) && ! follow && ! sampleLocked.load (std::memory_order_relaxed))
    {
        // LOOP へ戻すときは今の位置に Cue の頭を置く。
        bHostAnchor.store (latestHostPosition.load (std::memory_order_acquire), std::memory_order_relaxed);
        bSourceAnchor.store (cueStart.load (std::memory_order_relaxed), std::memory_order_relaxed);
    }
    followA.store (follow, std::memory_order_relaxed);
    followOffset.store (offset, std::memory_order_relaxed);
    if (gain) bLinearGain.store (*gain, std::memory_order_release);
    normalSwitchSerial.fetch_add (1, std::memory_order_acq_rel);
    mappingGeneration.fetch_add (1, std::memory_order_release);
    normalSwitchWriting.store (false, std::memory_order_release);
}

void RuntimeV2Controller::requestFollowPages() noexcept
{
    // 繰り返す Cue の頭と終わりのページは LOOP の間だけ押さえる（FOLLOW A は曲の先を読む）。
    const bool follow = followA.load (std::memory_order_acquire);
    currentPages().setPinnedCue (cueStart.load (std::memory_order_acquire), cueEnd.load (std::memory_order_acquire),
                                 ! follow && (cueLoops.load (std::memory_order_acquire) || restartsAtCueStart()));
    if (const auto position = mappedSourcePosition (latestHostPosition.load (std::memory_order_acquire)); position >= 0)
        currentPages().request (position);
}

// B・C の流し方（メッセージスレッド）。鳴っている役は新しい流し方の MATCH を先に決め、写像と gain を一組で渡す。決められ
// なければ新しい写像は無音（gain 0）で渡して A へ戻す（上限を超える音を一度も出さない）。戻しの gain は捨てる。
bool RuntimeV2Controller::setFollowA (bool follow, double aLoudness, double aSessionPeakDbtp) noexcept
{
    if (versionComparison || followA.load (std::memory_order_acquire) == follow) return true;
    const bool selected = bSelected.load (std::memory_order_acquire);
    const bool audible = selected || normalReturnToken.load (std::memory_order_acquire) || normalAudible.load (std::memory_order_acquire);
    const auto generation = normalSelectionGeneration.load (std::memory_order_acquire);
    PreparedNormalSelection prepared;
    const auto match = selected ? matchForMode (follow, aLoudness, aSessionPeakDbtp, prepared) : ModeMatch::failed;
    {
        const juce::ScopedLock lock (mappingWriteLock);
        std::optional<float> gain;
        if (audible && match == ModeMatch::matched) gain = prepared.linearGain;
        else if (audible && match == ModeMatch::failed) gain = 0.0f;
        writeFollowMappingLocked (follow, followOffset.load (std::memory_order_relaxed), gain);
    }
    requestFollowPages();
    {
        const juce::ScopedLock lock (stateLock);
        if (match == ModeMatch::matched && normalSelectionGeneration.load (std::memory_order_acquire) == generation)
        {
            beginMatchLocked();
            applyPreparedFactsLocked (prepared);
            return true;
        }
        if (match == ModeMatch::unchanged) return true;
        heldSelection = {};  // 前の流し方で決めた gain は新しい流し方で確かめていない
    }
    if (! selected) return true;
    selectA();  // 新しい写像は無音。A へ戻して、呼ぶ側が新しい MATCH の押し直しの待ちを立てる
    return false;
}

// FOLLOW A の手のずらし（秒。正で参照曲が先へ進む）。±10 分まで。曲・区間・形式が替わると作業スレッドが 0 に戻す。
// FOLLOW A の gain は曲全体の値で決めたので位置に依らず、同じ gain のまま新しい位置へクロスフェードする。
void RuntimeV2Controller::setFollowOffsetSeconds (double seconds) noexcept
{
    if (versionComparison || ! std::isfinite (seconds)) return;
    double rate = 0.0;
    { const juce::ScopedLock state (stateLock); rate = requestedConfiguration.sampleRate; }
    const auto samples = rate > 0.0 ? static_cast<std::int64_t> (std::llround (juce::jlimit (-600.0, 600.0, seconds) * rate)) : 0;
    {
        const juce::ScopedLock lock (mappingWriteLock);
        if (followOffset.load (std::memory_order_relaxed) == samples) return;
        // LOOP の写像はずらしを読まない（FOLLOW A へ替える書き込みが同じ鍵の後で読む）ので、写像を書き換えない。
        if (! followA.load (std::memory_order_relaxed)) { followOffset.store (samples, std::memory_order_relaxed); return; }
        writeFollowMappingLocked (true, samples, std::nullopt);
    }
    requestFollowPages();
}

double RuntimeV2Controller::followOffsetSeconds() const noexcept
{
    const juce::ScopedLock state (stateLock);
    const auto rate = requestedConfiguration.sampleRate;
    return rate > 0.0 ? static_cast<double> (followOffset.load (std::memory_order_acquire)) / rate : 0.0;
}
}
