#include "ReferenceComparisonController.h"
#include <algorithm>
#include <cmath>

namespace hypha::reference_audition
{
bool ReferenceComparisonController::selectB (double loudness, double peak) noexcept
{
    clearPendingAudition(); switchSlot.store (0, std::memory_order_release);
    if (trialActive() || ! snapshot().versionReady) return false;
    reference.selectA(); check.selectA();  // 切り替えの隙間（A が聴こえる時間）を広げない順
    const bool selected = version.selectB (loudness, peak);
    normalOutputSlot.store (selected ? 1 : 0, std::memory_order_release);
    return selected;
}
bool ReferenceComparisonController::selectC (double loudness, double peak) noexcept
{
    clearPendingAudition(); switchSlot.store (0, std::memory_order_release);
    if (trialActive() || ! snapshot().checkReady) return false;
    reference.selectA(); version.selectA();
    const bool selected = check.selectB (loudness, peak);
    normalOutputSlot.store (selected ? 2 : 0, std::memory_order_release);
    return selected;
}
// B（REF）。B セットの曲を、A の直近 10 秒に追従する gain で鳴らす。
bool ReferenceComparisonController::selectRef (double loudness, double peak) noexcept
{
    clearPendingAudition(); switchSlot.store (0, std::memory_order_release);
    if (trialActive() || ! snapshot().referenceReady) return false;
    version.selectA(); check.selectA();
    const bool selected = reference.selectB (loudness, peak);
    normalOutputSlot.store (selected ? 3 : 0, std::memory_order_release);
    return selected;
}
void ReferenceComparisonController::selectA() noexcept
{ clearPendingAudition(); dropResume(); version.selectA(); check.selectA(); reference.selectA(); }

// 2026-10-03（R-12）：上限を超えた MATCH の役を、承認して A を差だけ下げて合わせる（参照は元の音量）。
// 下げ終わってから、押したのと同じ待ちで鳴らす（再生中でも止まっていても）。深くするだけで、浅くするのは RETURN。
// 下げるのは利用者が承認した量（承認のボタンに出した量）。鳴らす待ちを立てられなければ下げない。
bool ReferenceComparisonController::approveLowerAAndPlay (int slot, double approvedDb)
{
    if ((slot != 1 && slot != 2 && slot != 3) || trialActive()
        || outputDecision (output_owner::Activity::lowerA).refused()) return false;
    if (! std::isfinite (approvedDb) || approvedDb >= 0.0
        || slotController (slot).snapshot().matchFailure != MatchFailure::ceilingExceeded) return false;
    const auto before = heldA.targetDb();
    heldA.hold (approvedDb);
    for (auto* role : { &version, &check, &reference }) role->setHeldAttenuation (heldA.targetDb());
    if (queueAudition (slot, pendingSafetyEpoch.load (std::memory_order_acquire)))
    {
        const juce::ScopedLock lock (selectionLock);
        pendingAudition.approvedLowerA = true;
        return true;
    }
    heldA.restore (before);
    for (auto* role : { &version, &check, &reference }) role->setHeldAttenuation (heldA.targetDb());
    return false;
}

// RETURN：鳴っている役を止めてから A を通常の音量へ（0.5 秒で上げる）。役の gain は下げた A に合わせてあり、
// そのまま上げると上限を超えるので、先に止める。上げ始めるのは、役が出力を返した後（Audio Thread が決める）。
// 下げた量で合わせた戻す保留も忘れる。
void ReferenceComparisonController::returnAToNormalLevel()
{
    selectA();
    forgetHeldAudition();
    heldA.requestReturn();
    for (auto* role : { &version, &check, &reference }) role->setHeldAttenuation (0.0);
}
bool ReferenceComparisonController::startBlind (double loudness, double peak) noexcept
{
    if (trialActive() || ! snapshot().versionReady || !beginBlindGuard()) return false;
    dropResume(); check.suspendAudition(); reference.suspendAudition(); version.selectA(); viewedSlot.store (1, std::memory_order_release);
    const bool started=version.startBlind(loudness,peak); if(!started) finishVersionBlindSession(); return started;
}
bool ReferenceComparisonController::approveBlindLowerAAndStart (double loudness, double peak) noexcept
{
    if (trialActive() || !snapshot().versionReady || !beginBlindGuard()) return false;
    dropResume(); check.suspendAudition(); reference.suspendAudition(); version.selectA(); viewedSlot.store (1, std::memory_order_release);
    const bool started=version.approveBlindLowerAAndStart(loudness,peak); if(!started) finishVersionBlindSession(); return started;
}
bool ReferenceComparisonController::selectBlindStimulus (int value) noexcept { return version.selectBlindStimulus (value); }
bool ReferenceComparisonController::answerBlind (int value) noexcept { return version.answerBlind (value); }
bool ReferenceComparisonController::revealBlind() noexcept { return version.revealBlind(); }
// END と、REF を離れる・窓を閉じるとき。中の Blind が無ければ何もしない（聴いている V はそのまま）。
void ReferenceComparisonController::endBlind() noexcept
{
    version.endBlind();
    if (blindSessionOpen.load (std::memory_order_acquire)) finishVersionBlindSession();
}
void ReferenceComparisonController::suspendAudition() noexcept
{
    clearPendingAudition(); dropResume(); version.suspendAudition(); check.suspendAudition(); reference.suspendAudition();
    reconcileVersionBlindSession();  // ライセンスを失ったときは定期の処理がここだけを呼ぶ
}

}

namespace hypha::reference_audition
{
// B の曲を選ぶ。B が鳴っていた（または戻る保留・押した後の待ちがある）なら、新しい曲が公開され次第、
// 新しい MATCH で B のまま鳴らす（「押せば即切替」）。それまでのあいだは A（フェードで戻す）。違う曲を前の
// gain で鳴らすことはしない。ほかの役（C・V）は止めない。
bool ReferenceComparisonController::selectSong (const juce::String& id)
{
    if (trialActive() || ! reference.selectLibrarySong (id)) return false;
    { const juce::ScopedLock lock (selectionLock); songId = id; }
    continueAfterSwitch (3);
    if (stateChanged) stateChanged();
    return true;
}

bool ReferenceComparisonController::selectSongSet (const juce::String& id)
{
    const auto state = reference.snapshot();
    if (std::none_of (state.songSets.begin(), state.songSets.end(), [&] (const auto& set) { return set.id == id; }))
        return false;
    { const juce::ScopedLock lock (selectionLock); songSetId = id; }
    if (stateChanged) stateChanged();
    return true;
}

// B の曲をまだ選んでいない（初めて）ときは、選んでいる B SET の最初の曲（準備済みを先に）を選んでおく。
// B を押せばすぐ鳴るように、音の準備は前もって進める。選んだ曲が B セットから外れたときは黙って替えない
// （B の画面が「保存した選択が無い / 選び直す」を出す。R-28）。
void ReferenceComparisonController::ensureReferenceSong (const Snapshot& r)
{
    if (r.songSets.empty()) return;
    juce::String setId, current;
    { const juce::ScopedLock lock (selectionLock); setId = songSetId; current = songId; }
    if (current.isNotEmpty()) return;
    const auto found = std::find_if (r.songSets.begin(), r.songSets.end(), [&] (const auto& set) { return set.id == setId; });
    const auto& set = found != r.songSets.end() ? *found : r.songSets.front();
    const RuntimeSelectionOption* pick = nullptr;
    for (const auto& song : set.songs) if (! song.requiresPreparation) { pick = &song; break; }
    if (pick == nullptr && ! set.songs.empty()) pick = &set.songs.front();
    if (pick == nullptr || ! reference.selectLibrarySong (pick->id)) return;
    const juce::ScopedLock lock (selectionLock);
    if (songId == current) songId = pick->id;
}
}
