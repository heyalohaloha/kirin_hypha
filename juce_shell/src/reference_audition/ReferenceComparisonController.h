#pragma once
#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceVisualObservation.h"
#include "ReferenceACaptureSession.h"
#include "ReferenceACaptureProjection.h"

#include <deque>
#include <juce_events/juce_events.h>

namespace hypha::reference_audition
{
// A is the original DAW input. B and C own separate prepared choices, while one
// shared gate admits only the explicitly selected output path.
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
    ReferenceComparisonSettings savedSettings();
    void restoreSettings (const ReferenceComparisonSettings&);
    bool selectVersion (const juce::String&);
    bool selectPreset (const juce::String&);
    bool selectCheck (const juce::String&);
    bool selectCandidate (const juce::String&);
    bool selectCue (const juce::String&);
    bool retryPresetSelection();
    bool retryCandidatePreparation();
    bool approveSampleRateConversion();
    bool requestRecovery();
    bool startLatestReview();
    bool startLatestBookmark();
    bool moveWorkflow (int direction, bool confirmed, bool deferred);
    void endWorkflow();
    void setCaptureTonalRange (double startSeconds, double endSeconds);
    bool selectB (double, double) noexcept;
    bool selectC (double, double) noexcept;
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
    std::optional<ReferenceComparisonSettings> pendingSettings;
    bool configured = false;
    std::atomic<int> viewedSlot { 2 }, normalOutputSlot { 0 };
    std::atomic<bool> versionChosen { false };
    bool rtPlaying = false, rtInputAllowed = false;
    juce::AudioBuffer<float> bScratch { 2, 8192 }, cScratch { 2, 8192 };
    std::shared_ptr<ReferenceAnalysis> analysis=std::make_shared<ReferenceAnalysis>();
    std::shared_ptr<WorkflowCommitInbox> workflowCommitInbox = std::make_shared<WorkflowCommitInbox>();
    juce::CriticalSection workflowServiceLock;
    RuntimeV2Controller version, check;
    VisualObservation visual;
    ACaptureSession capture;
    ACaptureProjection captureProjection;
    std::shared_ptr<VisualPreferences> visualPreferences = std::make_shared<VisualPreferences>();
};
}
