#pragma once
#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceVisualObservation.h"
#include "ReferenceACaptureSession.h"
#include "ReferenceACaptureProjection.h"

#include <deque>
#include <juce_events/juce_events.h>

namespace hypha::reference_audition
{
// A is the original DAW input. V (slot 1, Version), C (slot 2, Check) and B (slot 3, REF: the
// B set songs, H8) own separate prepared choices, while one shared gate admits only the explicitly
// selected output path. Only one role sounds at a time.
class ReferenceComparisonController final : private juce::AsyncUpdater
{
public:
    using SelectionGate = RuntimeV2Controller::SelectionGate;
    using StateChanged = std::function<void()>;
    explicit ReferenceComparisonController (juce::File, SelectionGate = {}, SelectionGate = {},
                                            SelectionGate = {}, StateChanged = {});
    ~ReferenceComparisonController() override;
    void setAnalysisOwner(KirinReferenceAnalysisOwner* owner) { analysis->replace(owner); }
    void configure (RuntimeIdentity, double, int);
    void setPresented (bool active) noexcept;
    Snapshot snapshot();
    bool captureObservationReady() const noexcept { return version.captureObservationReady(); }
    bool captureObservationQueueDrained() const noexcept { return version.captureObservationQueueDrained(); }
    ReferenceComparisonSettings savedSettings();
    void restoreSettings (const ReferenceComparisonSettings&);
    bool selectVersion (const juce::String&);
    bool selectPreset (const juce::String&);
    bool selectCheck (const juce::String&);
    bool selectCandidate (const juce::String&);
    bool selectCue (const juce::String&);
    bool selectVisualSlot (int); // Display only; never changes the audible source or gain.
    bool retryPresetSelection();
    bool retryCandidatePreparation();
    bool approveSampleRateConversion(int slot);
    bool requestRecovery();
    bool startLatestReview();
    bool startLatestBookmark();
    bool moveWorkflow (int direction, bool confirmed, bool deferred);
    void endWorkflow();
    void setCaptureTonalRange (double startSeconds, double endSeconds);
    bool selectB (double, double) noexcept;
    bool selectC (double, double) noexcept;
    bool selectRef (double, double) noexcept;              // H8: B（REF）を鳴らす
    bool selectSong (const juce::String& songId);          // H8: B の曲。B が鳴っていれば即切替
    bool selectSongSet (const juce::String& songSetId);    // H8: B SET（Hypha に届いた順位 1〜3）
    bool requestAudition (int slot, double, double); // Explicit click; stopped transport queues only.
    void servicePendingAudition (double, double, bool callbackLive);
    bool pendingAuditionNeedsService() const;
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
    void observeAInput (const juce::AudioBuffer<float>&, std::int64_t, bool, bool, bool, int clock = 0, std::optional<bool> captureAllowed = {}, CaptureClockSignature = {}) noexcept;
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

private:
    struct PendingWorkflowTransition
    {
        enum class Action { checkpointOnly, move, finish };
        Action action = Action::checkpointOnly;
        juce::String operationId;
        std::shared_ptr<const WorkflowDefinition> definition;
        int nextIndex = 0;
    };
    struct WorkflowCommitInbox
    {
        juce::CriticalSection lock;
        std::deque<WorkflowEventCommit> commits;
        juce::AsyncUpdater* updater = nullptr;
        bool accepting = true;
    };
    bool admit (int, bool);
    bool admitCapture(bool);
    bool beginBlindGuard();
    void endBlindGuard();
    void refreshObservation();
    ACaptureReceipt captureReceipt() const;
    RuntimeV2Controller& viewed() noexcept;
    bool trialActive() const;
    bool startWorkflow (std::shared_ptr<const WorkflowDefinition>, int itemIndex = 0);
    bool prepareWorkflowItem (const std::shared_ptr<const WorkflowDefinition>&, int);
    bool appendWorkflowEvent (const juce::String&, const WorkflowItem&,
                              const std::shared_ptr<const WorkflowDefinition>&,
                              PendingWorkflowTransition::Action, int nextIndex = 0);
    void workflowCommitted (const WorkflowEventCommit&);
    void serviceWorkflowCommits();
    void handleAsyncUpdate() override;
    bool hasActiveWorkflow() const;
    void clearPendingAudition();
    void appendPendingAudition (Snapshot&, const VisualBinding&, const VisualBinding&) const;
    bool finishWorkflow (bool completed);
    void applyWorkflowFinish();
    SelectionGate gate, captureGate, blindCaptureGate;
    bool captureOwned=false,blindGuardOwned=false,localBlindOwned=false;
    std::uint64_t localBlindEpoch=0;
    std::atomic<bool> presented{false};
    juce::CriticalSection gateLock;
    bool closing = false;
    int gateOwners = 0; // Bit mask retains one external admission across overlapping tails.
    mutable juce::CriticalSection selectionLock;
    juce::String versionId, receiverId;
    TonalDisplayState tonalState;
    WorkflowResumeState workflowState;
    std::shared_ptr<const WorkflowDefinition> activeWorkflow;
    std::optional<PendingWorkflowTransition> pendingWorkflowTransition;
    ReferenceChoice normalCheckChoice;
    int workflowItemIndex = 0;
    StateChanged stateChanged;
    struct PendingIntent
    {
        PendingAuditionView view;
        juce::String identity;
        std::uint64_t safetyEpoch = 0;
        std::uint64_t intentId = 0;
        bool sawPlayback = false;
        bool resume = false; // H5: 利用者の選択を同じ音・同じ gain で戻す（新しい MATCH はしない）
    } pendingAudition; // selectionLock; control thread only.
    bool resumeWanted() const;
    bool armResume();
    void dropResume();
    RuntimeV2Controller& slotController (int slot) noexcept { return slot == 1 ? version : slot == 3 ? reference : check; }
    const RuntimeV2Controller& slotController (int slot) const noexcept { return slot == 1 ? version : slot == 3 ? reference : check; }
    void ensureReferenceSong (const Snapshot& reference);
    juce::String songSetId, songId; // H8: selectionLock
    std::atomic<bool> songSwitchPending { false }; // H8: B のまま別の曲に替えた。公開されたら新しい MATCH で鳴らす
    std::uint64_t pendingSequence = 0;
    std::atomic<std::uint64_t> activePendingIntent { 0 };
    std::atomic<std::uint64_t> pendingSafetyEpoch { 0 };
    std::atomic<int> pendingInputSafety { -1 }; // -1: no callback yet, 0: forbidden, 1: allowed.
    std::optional<ReferenceComparisonSettings> pendingSettings;
    bool configured = false;
    std::atomic<int> viewedSlot { 2 }, normalOutputSlot { 0 };
    std::atomic<bool> versionChosen { false };
    bool rtPlaying = false, rtInputAllowed = false, rtInputObserved = false;
    juce::AudioBuffer<float> bScratch { 2, 8192 }, cScratch { 2, 8192 }, rScratch { 2, 8192 };
    std::shared_ptr<ReferenceAnalysis> analysis=std::make_shared<ReferenceAnalysis>();
    std::shared_ptr<WorkflowCommitInbox> workflowCommitInbox = std::make_shared<WorkflowCommitInbox>();
    juce::CriticalSection workflowServiceLock;
    RuntimeV2Controller version, check, reference;
    VisualObservation visual;
    ACaptureSession capture;
    ACaptureProjection captureProjection;
    std::shared_ptr<VisualPreferences> visualPreferences = std::make_shared<VisualPreferences>();
};
}
