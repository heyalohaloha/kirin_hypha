#include "kirin_hypha_reference_capture_ffi.h"
#include "PluginProcessor.h"
#include "reference_audition/ReferenceLiveWindowLoudness.h"

#include <cmath>
#include <limits>
#include <thread>


hypha::reference_audition::Snapshot KirinHyphaProcessorBase::referenceAuditionSnapshot() const
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return referenceAuditionController != nullptr
        ? referenceAuditionController->snapshot()
        : hypha::reference_audition::Snapshot {};
   #else
    return {};
   #endif
}

void KirinHyphaProcessorBase::setReferenceViewPresented (bool active)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController) referenceAuditionController->setPresented (active);
   #else
    juce::ignoreUnused (active);
   #endif
}

hypha::reference_audition::LiveALevel KirinHyphaProcessorBase::referenceLiveALevel (bool windowOnly, int windowBlocks, int minimumBlocks) const
{
    namespace ref = hypha::reference_audition;
    KirinObservatoryFrame frame {};
    const bool received = pollObservatoryFrame (frame);
    auto level = ref::liveALevel (frame, received, heartbeatLive(), isPlaying());
    if (! std::isfinite (level.loudness)) return level;
    // A の音量は直近の窓（B・V は 10 秒、C は Cue と同じ長さ）のゲートつき音量（積算の Integrated は
    // 使わない）。Peak と上限はセッションの max TP のまま。窓が 3 秒に満たないあいだ（再生を始めた直後・
    // シークの後）は、選ぶときは積算の値を使い、追従と C（windowOnly）では値なしにして待たせる・直前の gain を
    // 保たせる。C は窓に minimumBlocks（Cue の長さ、最長 30 秒）たまるまで値なし（仕様 C）。
   #if ! KIRIN_HYPHA_PRE_DISPLAY  // Reference の試聴は POST だけ（PRE は窓の計算を持たない）
    const auto blocks = static_cast<size_t> (juce::jmax (1, windowBlocks));
    std::vector<KirinMeterHistoryEntry> history;
    // 計測スレッドが A の履歴に書き足しているあいだは読めない（try_lock）。読めなかっただけで「A がたまって
    // いない」（画面の A 0）・積算の値（B・V の MATCH が黙って窓と違う音量で合う）にしない。2 ms まで読み直す
    // （メッセージスレッド。計測スレッドが持つのは 1 回の書き足しのあいだだけ）。
    const auto giveUpMs = juce::Time::getMillisecondCounterHiRes() + 2.0;
    bool read = pollMeterHistory (KIRIN_METER_HISTORY_10_HZ, history, blocks, blocks);
    while (! read && juce::Time::getMillisecondCounterHiRes() < giveUpMs)
    {
        std::this_thread::yield();
        read = pollMeterHistory (KIRIN_METER_HISTORY_10_HZ, history, blocks, blocks);
    }
    const auto window = read ? ref::liveWindowLoudness (history, windowBlocks) : ref::LiveWindowLoudness {};
    level.windowBlocks = window.blocks;
    level.windowUnread = ! read;
    if (window.gatedBlocks >= ref::liveWindowMinimumGatedBlocks && window.blocks >= minimumBlocks && std::isfinite (window.lufs))
        level.loudness = window.lufs;
    else if (windowOnly)
        level.loudness = std::numeric_limits<double>::quiet_NaN();
   #else
    juce::ignoreUnused (windowOnly, windowBlocks, minimumBlocks);
   #endif
    return level;
}

// 画面が「同じ音量」にそろえて比べるための A の窓の音量（その役の窓：B・V は 10 秒、C は Cue と
// 同じ長さ）。C は MATCH と同じく、A の直近が Cue の長さ（最長 30 秒）たまるまで値なし（仕様 C。画面の
// 「鳴らすときの gain」と実際の MATCH をそろえる）。メーター履歴は 250 ms に 1 回だけ読む（メッセージスレッド）。
hypha::reference_audition::WindowLoudnessCache KirinHyphaProcessorBase::referenceWindowLoudness (int slot) const
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    const auto now = juce::Time::getMillisecondCounterHiRes();
    auto& cache = referenceWindowCache;
    if (cache.slot != slot || now >= cache.validUntilMs)
    {
        const auto blocks = referenceAuditionController != nullptr ? referenceAuditionController->liveWindowBlocks (slot)
                                                                   : hypha::reference_audition::liveWindowBlocks;
        const auto needed = hypha::reference_audition::matchMinimumBlocks (slot, blocks);
        const auto level = referenceLiveALevel (true, blocks, needed);
        // 読み直しても読めなかったら前の値のまま、次の描画で読み直す（A 0 と出さない）。
        if (level.windowUnread && cache.slot == slot) cache.validUntilMs = now;
        else cache = { slot, level.loudness, now + 250.0, level.windowBlocks, needed };
    }
    return cache;
   #else
    juce::ignoreUnused (slot);
    return {};
   #endif
}

// C の MATCH をもう一度。A の直近（Cue と同じ長さ）で鳴っている C の gain を決め直して固定する。
hypha::reference_audition::RematchResult KirinHyphaProcessorBase::rematchReferenceCheck()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController == nullptr) return hypha::reference_audition::RematchResult::notPlaying;
    const auto blocks = referenceAuditionController->liveWindowBlocks (2);
    const auto level = referenceLiveALevel (true, blocks, hypha::reference_audition::matchMinimumBlocks (2, blocks));
    return referenceAuditionController->rematch (2, level.loudness, level.peak);
   #else
    return hypha::reference_audition::RematchResult::notPlaying;
   #endif
}

// V の自動特定。A の直近 30 秒の Kirin 指紋を Version の指紋（ranges）と照合する。
hypha::reference_audition::VersionIdentity KirinHyphaProcessorBase::identifyReferenceVersion()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController != nullptr) return referenceAuditionController->identifyVersions();
   #endif
    return {};
}

bool KirinHyphaProcessorBase::selectReferenceB() { return requestReferenceAudition (1); }
bool KirinHyphaProcessorBase::selectReferenceC() { return requestReferenceAudition (2); }
bool KirinHyphaProcessorBase::selectReferenceRef() { return requestReferenceAudition (3); }  // B（REF）

// B の曲と B SET。B が鳴っていれば、新しい曲が準備でき次第 B のまま鳴る。
bool KirinHyphaProcessorBase::selectReferenceSong (const juce::String& id)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    const bool selected = licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectSong (id);
    if (selected && referencePendingAuditionNeedsService()) startTimer (50);
    return selected;
   #else
    juce::ignoreUnused (id);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceSongSet (const juce::String& id)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectSongSet (id);
   #else
    juce::ignoreUnused (id);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::requestReferenceAudition (int slot)
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! licenseIsOs())
    {
        if (referenceAuditionController != nullptr)
            referenceAuditionController->suspendAudition();
        return false;
    }
    const auto blocks = referenceAuditionController != nullptr ? referenceAuditionController->liveWindowBlocks (slot)
                                                               : hypha::reference_audition::liveWindowBlocks;
    const auto level = referenceLiveALevel (slot == 2, blocks, hypha::reference_audition::matchMinimumBlocks (slot, blocks));
    const bool accepted = referenceAuditionController != nullptr
        && referenceAuditionController->requestAudition (slot, level.loudness, level.peak);
    if (accepted && (referencePendingAuditionNeedsService() || referenceTrackingNeedsService())) startTimer (50);
    return accepted;
   #else
    juce::ignoreUnused (slot);
    return false;
   #endif
}

void KirinHyphaProcessorBase::selectReferenceA()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController != nullptr)
        referenceAuditionController->selectA();
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceVersion (const juce::String& id, bool automatic)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectVersion (id, automatic);
   #else
    juce::ignoreUnused (id, automatic);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferencePreset (const juce::String& id)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectPreset (id);
   #else
    juce::ignoreUnused (id);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::retryReferencePresetSelection()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->retryPresetSelection();
   #else
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceCheck (const juce::String& id)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectCheck (id);
   #else
    juce::ignoreUnused (id);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceCandidate (const juce::String& id)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectCandidate (id);
   #else
    juce::ignoreUnused (id);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::retryReferenceCandidatePreparation()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->retryCandidatePreparation();
   #else
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceCue (const juce::String& id)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectCue (id);
   #else
    juce::ignoreUnused (id);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceVisualSlot (int slot)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->selectVisualSlot (slot);
   #else
    juce::ignoreUnused (slot);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::approveReferenceSampleRateConversion(int slot)
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->approveSampleRateConversion(slot);
   #else
    juce::ignoreUnused (slot);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::requestReferenceRecovery()
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return licenseIsOs() && referenceAuditionController != nullptr
        && referenceAuditionController->requestRecovery();
   #else
    return false;
   #endif
}

bool KirinHyphaProcessorBase::startReferenceBlind (double aIntegratedLoudness,
                                                   double aMaximumTruePeakDbtp)
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! licenseIsOs())
    {
        if (referenceAuditionController != nullptr)
            referenceAuditionController->suspendAudition();
        return false;
    }
    return referenceAuditionController != nullptr
        && referenceAuditionController->startBlind (
            aIntegratedLoudness, aMaximumTruePeakDbtp);
   #else
    juce::ignoreUnused (aIntegratedLoudness, aMaximumTruePeakDbtp);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::selectReferenceBlindStimulus (int stimulus)
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! licenseIsOs())
    {
        if (referenceAuditionController != nullptr)
            referenceAuditionController->suspendAudition();
        return false;
    }
    return referenceAuditionController != nullptr
        && referenceAuditionController->selectBlindStimulus (stimulus);
   #else
    juce::ignoreUnused (stimulus);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::approveReferenceBlindLowerA (
    double aIntegratedLoudness, double aMaximumTruePeakDbtp)
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! licenseIsOs())
    {
        if (referenceAuditionController != nullptr)
            referenceAuditionController->suspendAudition();
        return false;
    }
    return referenceAuditionController != nullptr
        && referenceAuditionController->approveBlindLowerAAndStart (
            aIntegratedLoudness, aMaximumTruePeakDbtp);
   #else
    juce::ignoreUnused (aIntegratedLoudness, aMaximumTruePeakDbtp);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::answerReferenceBlind (int stimulus)
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! licenseIsOs())
    {
        if (referenceAuditionController != nullptr)
            referenceAuditionController->suspendAudition();
        return false;
    }
    return referenceAuditionController != nullptr
        && referenceAuditionController->answerBlind (stimulus);
   #else
    juce::ignoreUnused (stimulus);
    return false;
   #endif
}

bool KirinHyphaProcessorBase::revealReferenceBlind()
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! licenseIsOs())
    {
        if (referenceAuditionController != nullptr)
            referenceAuditionController->suspendAudition();
        return false;
    }
    return referenceAuditionController != nullptr
        && referenceAuditionController->revealBlind();
   #else
    return false;
   #endif
}

void KirinHyphaProcessorBase::endReferenceBlind()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController != nullptr)
        referenceAuditionController->endBlind();
   #endif
}

#if ! KIRIN_HYPHA_PRE_DISPLAY
void KirinHyphaProcessorBase::createReferenceAuditionController()
{
    referenceAuditionController = std::make_unique<hypha::reference_audition::ReferenceComparisonController> (
        hypha::reference_audition::RuntimeV2Repository::transportRoot(), [this] (bool active)
        {
            const juce::ScopedLock gateLock (handleLock);
            return hyphaHandle != nullptr
                && kirin_hypha_set_reference_audition_active (hyphaHandle, active);
        }, [this](bool active) {
            const juce::ScopedLock lock(handleLock);
            return hyphaHandle && kirin_hypha_set_version_blind_capture_exclusion(hyphaHandle,active);
        }, [this] { captureStateNotification.changed(); });
}
#endif


void KirinHyphaProcessorBase::configureReferenceAudition()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (role != Role::Post) return;
    if (! stereoWorkflowsSupported())
    {
        referenceAuditionController.reset();
        return;
    }
    if (referenceAuditionController == nullptr) createReferenceAuditionController();
    hypha::reference_audition::RuntimeIdentity identity;
    identity.runtimeInstanceId = referenceRuntimeId;
    identity.library = true;
    referenceAuditionController->setAnalysisOwner(kirin_hypha_reference_analysis_owner(hyphaHandle));
    referenceAuditionController->configure (identity, preparedFormat.sampleRate, static_cast<int> (preparedFormat.channelRoles.size()));
   #endif
}
