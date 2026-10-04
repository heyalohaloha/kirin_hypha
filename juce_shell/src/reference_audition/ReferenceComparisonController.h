#pragma once
#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceVisualObservation.h"
#include "ReferenceAInputRelay.h"
#include "ReferenceBlindSlot.h"
#include "ReferenceComparisonSettings.h"
#include "ReferenceHeldAttenuation.h"

#include <deque>
#include <juce_events/juce_events.h>

namespace hypha::reference_audition
{
// A is the original DAW input. V (slot 1, Version), C (slot 2, Check) and B (slot 3, REF: the
// B set songs, H8) own separate prepared choices, while one shared gate admits only the explicitly
// selected output path. Only one role sounds at a time.
class ReferenceComparisonController final
{
public:
    using SelectionGate = RuntimeV2Controller::SelectionGate;
    using StateChanged = std::function<void()>;
    // gate：出力の経路（Rust の試聴の排他）。versionBlindGate：VERSION BLIND のあいだのほかの Blind との排他（同じ project・
    // 同じ process の別の Hypha も含む。Rust の kirin_hypha_set_version_blind_capture_exclusion）。
    explicit ReferenceComparisonController (juce::File, SelectionGate gate = {}, SelectionGate versionBlindGate = {},
                                            StateChanged = {});
    ~ReferenceComparisonController();
    void setAnalysisOwner(KirinReferenceAnalysisOwner* owner) { analysis->replace(owner); }
    void configure (RuntimeIdentity, double, int);
    void setPresented (bool active) noexcept;
    Snapshot snapshot();
    ReferenceComparisonSettings savedSettings();
    void restoreSettings (const ReferenceComparisonSettings&);
    bool selectVersion (const juce::String&, bool automatic = false); // automatic：H7 の AUTO（V の選択だけを替える）
    bool selectPreset (const juce::String&);
    bool selectCheck (const juce::String&);
    bool selectCandidate (const juce::String&);
    bool selectCue (const juce::String&);
    bool selectVisualSlot (int); // Display only; never changes the audible source or gain.
    bool retryPresetSelection();
    bool retryCandidatePreparation();
    bool approveSampleRateConversion(int slot);
    bool requestRecovery();
    bool selectB (double, double) noexcept;
    bool selectC (double, double) noexcept;
    bool selectRef (double, double) noexcept;              // H8: B（REF）を鳴らす
    bool selectSong (const juce::String& songId);          // H8: B の曲。B が鳴っていれば即切替
    bool selectSongSet (const juce::String& songSetId);    // H8: B SET（Hypha に届いた順位 1〜3）
    bool requestAudition (int slot, double, double); // Explicit click; stopped transport queues, playing waits while preparing.
    void servicePendingAudition (double, double, bool callbackLive);
    bool pendingAuditionNeedsService() const;
    int pendingSlot() const; // 押した後に待っている役（無ければ 0）
    // 仕様 A：オフライン書き出し（Audio Thread が知らせる）・live 比較の開始では、停止前の選択へ自動で戻さない。
    void noteOfflineRender() noexcept { offlineRenderSeen.store (true, std::memory_order_release); }
    void forgetHeldAudition();
    void selectA() noexcept;
    bool reserveLocalBlind();
    void bindLocalBlind(std::uint64_t);
    void releaseLocalBlind(std::uint64_t);
    bool startBlind (double, double) noexcept;
    bool approveBlindLowerAAndStart (double, double) noexcept;
    bool selectBlindStimulus (int) noexcept;
    bool answerBlind (int) noexcept;
    bool revealBlind() noexcept;
    void endBlind() noexcept;
    void suspendAudition() noexcept;
    void observeTransport (std::int64_t, bool, bool) noexcept;
    // allowed：ライセンスを含めて A を聞く・測る。inputAllowed：バイパス・書き出しでない（無ければ allowed）。
    void observeAInput (const juce::AudioBuffer<float>&, std::int64_t, bool, bool, bool, int clock = 0,
                        std::optional<bool> inputAllowed = {}, AInputClockSignature = {}) noexcept;
    bool renderSelectedB (juce::AudioBuffer<float>&, std::int64_t, bool, bool, bool) noexcept;
    // H3／H4：A 側の窓の長さ（10 Hz のブロック数）。追従する役は 10 秒、C（固定）は Cue と同じ長さ。
    int liveWindowBlocks (int slot) const;
    int pendingLiveWindowBlocks() const;
    // H5：利用者が B／C を選んだまま（停止のあいだも）。戻す保留を立てるために timer を回し続ける。
    bool auditionHeld() const noexcept { return normalOutputSlot.load (std::memory_order_acquire) != 0; }
    bool pendingAuditionNeedsLevel() const; // 新しい MATCH をする保留だけが A の音量を要る（戻すときは要らない）
    // H3：聴いている役が追従するなら、1 秒ごとに A の直近の履歴で gain を求め直す（メッセージスレッド）。
    bool trackingNeedsService() const noexcept;
    TrackingAction followAudition (const std::vector<KirinMeterHistoryEntry>&, double aSessionPeakDbtp);
    RematchResult rematch (int slot, double aLoudness, double aSessionPeakDbtp); // H12: C の MATCH をもう一度
    VersionIdentity identifyVersions(); // H7: A の直近の指紋で V を特定する（メッセージスレッド）
    // 2026-10-03（R-12）：上限を超えた MATCH の役を、承認した量だけ A を下げて合わせる。下げ終わってから鳴らす。
    bool approveLowerAAndPlay (int slot, double approvedDb);
    // RETURN：役を止めてから A を通常の音量へ（0.5 秒で上げる）。下げた量で合わせた保留も戻さない。
    void returnAToNormalLevel();
    double heldAttenuationDb() const noexcept { return heldA.targetDb(); }

private:
    bool admit (int, bool);
    bool beginBlindGuard();
    void endBlindGuard();
    void refreshObservation();
    RuntimeV2Controller& viewed() noexcept;
    bool trialActive() const;
    void clearPendingAudition();
    void appendPendingAudition (Snapshot&, const VisualBinding&, const VisualBinding&) const;
    SelectionGate gate, versionBlindGate;
    bool blindGuardOwned=false,localBlindOwned=false;
    bool aInputPaused = false;  // gateLock：VERSION BLIND を始めてから終えるまで、A を観測スレッドへ渡さない
    std::uint64_t localBlindEpoch=0;
    std::atomic<bool> presented{false};
    juce::CriticalSection gateLock;
    bool closing = false;
    int gateOwners = 0; // Bit mask retains one external admission across overlapping tails.
    mutable juce::CriticalSection selectionLock;
    juce::String versionId, receiverId;
    StateChanged stateChanged;
    struct PendingIntent
    {
        PendingAuditionView view;
        juce::String identity;
        std::uint64_t safetyEpoch = 0;
        std::uint64_t intentId = 0;
        bool sawPlayback = false;
        bool resume = false; // H5: 利用者の選択を同じ音・同じ gain で戻す（新しい MATCH はしない）
        bool switching = false; // 鳴っていた役の選択の替え（停止をまたいで待ち、失敗したら選択を手放す）
        bool approvedLowerA = false; // 承認して A を下げて鳴らす待ち（鳴らす時点の差まで下げ直せる）
        int lowerRetries = 0;
    } pendingAudition; // selectionLock; control thread only.
    bool resumeWanted() const;
    bool armResume();
    void dropResume();
    // 鳴っていた（戻る保留・押した後の待ちを含む）役の選択を替えた。新しい選択が公開されたら新しい MATCH で
    // その役のまま鳴らす（押せば即切替）。ほかの役は止めない。continues が false（新しい選択が Kirin OS の
    // 準備待ちで、まだ世代が進んでいない）なら、その役の保留と待ちを手放すだけ。
    void continueAfterSwitch (int slot, bool continues = true);
    bool waitWhilePreparing (int slot);
    bool queueAudition (int slot, std::uint64_t safetyEpoch);
    bool selectCheckRole (const std::function<bool()>& apply);
    RuntimeV2Controller& slotController (int slot) noexcept { return slot == 1 ? version : slot == 3 ? reference : check; }
    const RuntimeV2Controller& slotController (int slot) const noexcept { return slot == 1 ? version : slot == 3 ? reference : check; }
    void ensureReferenceSong (const Snapshot& reference);
    juce::String songSetId, songId; // H8: selectionLock
    std::atomic<int> switchSlot { 0 };        // 選択を替えた役（1〜3）。公開されたら新しい MATCH で鳴らす
    std::uint64_t switchGeneration = 0;        // selectionLock：その役の替えた後の選択の世代
    std::atomic<bool> offlineRenderSeen { false };
    std::uint64_t pendingSequence = 0;
    std::atomic<std::uint64_t> activePendingIntent { 0 };
    std::atomic<std::uint64_t> pendingSafetyEpoch { 0 };
    std::atomic<int> pendingInputSafety { -1 }; // -1: no callback yet, 0: forbidden, 1: allowed.
    std::optional<ReferenceComparisonSettings> pendingSettings;
    bool configured = false;
    std::atomic<int> viewedSlot { 2 }, normalOutputSlot { 0 };
    std::atomic<bool> versionChosen { false };
    bool versionAuto = false; // selectionLock：V の Version は AUTO が選んだ（利用者が選ぶと false）
    bool rtPlaying = false, rtInputAllowed = false, rtInputObserved = false;
    juce::AudioBuffer<float> bScratch { 2, 8192 }, cScratch { 2, 8192 }, rScratch { 2, 8192 };
    HeldAttenuation heldA;  // 承認して A（POST の出力全体）を下げている量。RETURN まで保つ
    std::shared_ptr<ReferenceAnalysis> analysis=std::make_shared<ReferenceAnalysis>();
    RuntimeV2Controller version, check, reference;
    VisualObservation visual;
    // A を観測スレッドへ渡す（Audio Thread）。aFeed：見せていて Blind の外のときだけ渡す。aWriters：片付けで、Audio Thread が
    // 渡し終えるのを待つ（2026-10-04、A の取り込みの部品をやめたときにそこから移した守り）。
    AInputRelay aInput;
    std::atomic<bool> aFeed { false };
    std::atomic<int> aWriters { 0 };
    BlindSlot blindSlot;  // VERSION BLIND とローカル Blind は 1 つだけ
    std::shared_ptr<VisualPreferences> visualPreferences = std::make_shared<VisualPreferences>();
};
}
