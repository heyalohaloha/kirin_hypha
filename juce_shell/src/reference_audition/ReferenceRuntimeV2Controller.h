#pragma once

#include <atomic>
#include <deque>
#include <functional>

#include "ReferenceAuditionController.h"
#include "ReferenceAudioPages.h"
#include "ReferencePresetAdoptionTransport.h"
#include "ReferencePresetSelectionTransport.h"
#include "ReferenceRecoveryTransport.h"
#include "ReferenceRuntimeV2Alignment.h"
#include "ReferenceRuntimeV2Blind.h"
#include "ReferenceRuntimeV2Presentation.h"
#include "ReferenceRuntimeV2Repository.h"
#include "ReferenceRuntimeEventTransport.h"
#include "ReferenceRuntimeABinding.h"
#include "ReferenceRuntimeACapture.h"
#include "ReferenceRuntimeV2Source.h"

namespace hypha::reference_audition
{
    class RuntimeV2Controller final : private juce::Thread
    {
    public:
        using SelectionGate = std::function<bool(bool)>;

        explicit RuntimeV2Controller (juce::File transportRootIn = RuntimeV2Repository::transportRoot(),
                                      SelectionGate = {});
        ~RuntimeV2Controller() override;

        void configure (RuntimeIdentity, double hostSampleRate, int hostChannels);
        void disconnect();
        Snapshot snapshot() const;
        bool selectPreset (const juce::String&);
        bool selectCheck (const juce::String&);
        bool selectCandidate (const juce::String&);
        bool selectCue (const juce::String&);
        bool retryPresetSelection();
        bool approveSampleRateConversion();
        bool requestRecovery();

        void observeTransport (std::int64_t hostPosition, bool positionValid,
                               bool playing) noexcept;
        void observeAInput (const juce::AudioBuffer<float>&,
                            std::int64_t hostPosition,
                            bool positionValid,
                            bool playing,
                            bool auditionAllowed) noexcept;
        bool selectB (double aIntegratedLoudness, double aMaximumTruePeakDbtp) noexcept;
        void selectA() noexcept;
        bool startBlind (double, double) noexcept;
        bool approveBlindLowerAAndStart (double, double) noexcept;
        bool selectBlindStimulus (int) noexcept;
        bool answerBlind (int) noexcept;
        bool revealBlind() noexcept;
        void endBlind() noexcept;
        bool renderSelectedB (juce::AudioBuffer<float>&, std::int64_t hostPosition,
                              bool positionValid, bool auditionAllowed = true) noexcept;
        void loseAudibleConfirmation() noexcept;

    private:
        struct Configuration
        {
            RuntimeIdentity identity;
            double sampleRate = 0.0;
            int channels = 0;
            std::uint64_t generation = 0;
        };

        struct RequestedSelection
        {
            juce::String presetId;
            juce::String checkId;
            juce::String candidateId;
            juce::String cueId;
            juce::String sampleRateApprovalKey;
            std::uint64_t generation = 0;
        };

        struct AuditionEventSession
        {
            RuntimeEventContext context;
            juce::String runId;
            juce::String startedEventId;
            juce::String completedEventId;
            std::uint64_t bConfirmationBaseline = 0;
            std::uint64_t aConfirmationBaseline = 0;
            bool returnRequested = false;
            bool startWritten = false;
        };

        struct BlindEventSession
        {
            RuntimeEventContext context;
            juce::String startedEventId;
            juce::String completedEventId;
            std::int64_t startedAtMs = 0;
            std::int64_t completedAtMs = 0;
            juce::String runtimeFingerprint;
            juce::var trialStart;
            juce::var trialCompleted;
            bool startWritten = false;
            bool completionPending = false;
        };

        struct PreparedNormalSelection
        {
            std::uint64_t auditionEpoch = 0;
            std::uint64_t selectionGeneration = 0;
            float linearGain = 1.0f;
            double appliedGainDb = 0.0;
            double aIntegratedLoudness = 0.0;
            double aMaximumTruePeakDbtp = 0.0;
            double adjustedBIntegratedLoudness = 0.0;
            double adjustedBMaximumTruePeakDbtp = 0.0;
            double loudnessDeltaBMinusA = 0.0;
            double truePeakDeltaBMinusA = 0.0;
            bool gainLimited = false;
            bool comparisonFallbackOriginal = false;
            bool valid = false;
        };

        void run() override;
        void applyConfiguration (const Configuration&);
        void refreshWorkspace (const Configuration&, std::int64_t nowMs);
        void publish (Snapshot);
        void publishReady (Snapshot, std::shared_ptr<const RuntimeSource>);
        void publishApprovalRequired (Snapshot, const juce::String& approvalKey);
        void publishLocked (Snapshot);
        bool requestSelection (const juce::String& kind, const juce::String& id);
        std::int64_t mappedSourcePosition (std::int64_t hostPosition) const noexcept;
        bool prepareReferenceGain (double aIntegratedLoudness,
                                   double aMaximumTruePeakDbtp,
                                   std::uint64_t selectionGeneration) noexcept;
        bool activatePreparedB (std::uint64_t selectionGeneration) noexcept;
        bool startBlindWithApproval (double aIntegratedLoudness,
                                     bool approveLowerA) noexcept;
        std::uint64_t acquireOutputGate() noexcept;
        void releaseOutputGate (std::uint64_t token) noexcept;
        void releaseActiveOutputGate() noexcept;
        void beginAuditionEventSession (std::uint64_t bBaseline) noexcept;
        void requestAuditionReturnEvent (std::uint64_t aBaseline) noexcept;
        void beginBlindEventSession (const RuntimeV2BlindSnapshot&) noexcept;
        void completeBlindEventSession (const RuntimeV2BlindSnapshot&) noexcept;
        void serviceRuntimeEvents();
        void serviceRecoveryAcknowledgement();
        void servicePresetSelectionAcknowledgement();
        void serviceDeferredAudioThreadActions();
        void revokeAuditionPublication() noexcept;
        void failClosedToA() noexcept;
        void invalidateBlind() noexcept;
        void failClosedToAFromAudioThread() noexcept;
        void invalidateBlindFromAudioThread() noexcept;

        const juce::File root;
        const SelectionGate selectionGate;
        RuntimeV2Repository repository;
        RuntimeABindingRepository aBindingRepository;
        RuntimeACapture aCapture;
        RuntimeV2SourceRepository sourceRepository;
        RuntimeV2MeasurementRepository measurementRepository;
        RuntimeV2AlignmentRepository alignmentRepository;
        RuntimeV2ProfileRepository profileRepository;
        RuntimeV2PresentationRepository presentationRepository;
        RecoveryTransport recoveryTransport;
        PresetSelectionTransport presetSelectionTransport;
        PresetAdoptionTransport presetAdoptionTransport;
        RuntimeEventTransport eventTransport;
        AudioPages pages;
        RuntimeV2Blind blind;
        mutable juce::CriticalSection stateLock;
        juce::CriticalSection outputGateLock;
        Configuration requestedConfiguration;
        RequestedSelection requestedSelection;
        std::uint64_t appliedConfigurationGeneration = 0;
        std::uint64_t appliedSelectionGeneration = 0;
        std::shared_ptr<const RuntimeWorkspace> workspace;
        std::optional<RuntimeABinding> activeABinding;
        RuntimeFiles activeRuntimeFiles;
        std::shared_ptr<const RuntimeSource> workerSource;
        std::shared_ptr<const RuntimeSource> publishedSource;
        juce::String activeSourceArtifactSha256;
        juce::String activeSourceKey;
        juce::String activeMappingKey;
        juce::String activeContentMappingKey;
        juce::String activePublishedSelectionKey;
        juce::String pendingApprovalKey;
        juce::String blindContextKey;
        juce::String blindPreparationKey;
        juce::String activePresetAdoptionKey;
        Snapshot currentSnapshot;
        PreparedNormalSelection preparedNormalSelection;
        RuntimeEventContext activeEventContext;
        RuntimeCandidate activeEventCandidate;
        RuntimeCue activeEventCue;
        std::shared_ptr<const RuntimeSource> activeEventSource;
        std::deque<AuditionEventSession> auditionEventSessions;
        std::optional<BlindEventSession> blindEventSession;
        std::optional<RecoveryRequest> pendingRecoveryRequest;
        std::optional<PresetSelectionRequest> pendingPresetSelectionRequest;
        std::optional<RuntimeGlobalPresetCatalogEntry> pendingPresetSelectionTarget;
        std::optional<RuntimeGlobalPresetCatalogEntry> failedPresetSelectionTarget;
        std::int64_t recoveryStatusExpiresAtMs = 0;
        std::int64_t presetSelectionWaitingSinceMs = 0;
        std::int64_t presetSelectionStatusExpiresAtMs = 0;
        std::atomic<bool> ready { false };
        std::atomic<bool> bSelected { false };
        std::atomic<float> bLinearGain { 1.0f };
        std::atomic<std::uint64_t> auditionEpoch { 1 };
        std::atomic<std::uint64_t> activeAuditionEpoch { 0 };
        std::atomic<std::uint64_t> normalSelectionGeneration { 1 };
        std::atomic<std::int64_t> latestHostPosition { 0 };
        std::atomic<bool> latestPositionValid { false };
        std::atomic<bool> latestPlaying { false };
        std::atomic<std::int64_t> cueStart { 0 };
        std::atomic<std::int64_t> cueEnd { 0 };
        std::atomic<bool> cueLoops { false };
        std::atomic<bool> sampleLocked { false };
        std::atomic<std::uint64_t> mappingGeneration { 0 };
        std::atomic<std::uint64_t> bAudibleConfirmations { 0 };
        std::atomic<std::uint64_t> aAudibleConfirmations { 0 };
        std::atomic<std::int64_t> bHostAnchor { 0 };
        std::atomic<std::int64_t> bSourceAnchor { 0 };
        std::atomic<std::uint64_t> nextOutputGateToken { 1 };
        std::atomic<std::uint64_t> activeOutputGateToken { 0 };
        std::atomic<std::uint64_t> gateReleasePendingToken { 0 };
        std::atomic<bool> auditionReturnPending { false };
    };
}
