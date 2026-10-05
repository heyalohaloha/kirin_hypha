#pragma once

#include <atomic>
#include <deque>
#include <functional>

#include "ReferenceAuditionController.h"
#include "ReferenceAudioPages.h"
#include "ReferenceCandidatePreparationTransport.h"
#include "ReferencePresetAdoptionTransport.h"
#include "ReferencePresetSelectionTransport.h"
#include "ReferenceRecoveryTransport.h"
#include "ReferenceRuntimeV2Alignment.h"
#include "ReferenceRuntimeV2Blind.h"
#include "ReferenceRuntimeV2Presentation.h"
#include "ReferenceComparisonSettings.h"
#include "ReferenceRuntimeV2Repository.h"
#include "ReferenceRuntimeEventTransport.h"
#include "ReferenceRuntimeABinding.h"
#include "ReferenceRuntimeACapture.h"
#include "ReferenceRuntimeV2Source.h"
#include "ReferenceTrackingGain.h"
#include "ReferenceVersionIdentify.h"
#include "ReferenceRuntimeV2SourceCache.h"
#include "ReferenceCalibrationObservation.h"
#include "ReferenceDeferredControl.h"
#include "ReferenceVisualTimeline.h"

namespace hypha::reference_audition
{
    class RuntimeV2Controller final : private juce::Thread
    {
    public:
        using SelectionGate = std::function<bool(bool)>;

        explicit RuntimeV2Controller (juce::File transportRootIn = RuntimeV2Repository::transportRoot(),
                                      SelectionGate = {}, bool wholeVersionComparison = false);
        ~RuntimeV2Controller() override;

        void configure (RuntimeIdentity, double hostSampleRate, int hostChannels);
        void disconnect();
        ReferenceChoice savedChoice() const;
        void restoreChoice (const ReferenceChoice&);
        Snapshot snapshot() const;
        VisualBinding visualBinding() const;
        bool selectPreset (const juce::String&);
        bool selectCheck (const juce::String&);
        bool selectCandidate (const juce::String&);
        bool selectCue (const juce::String&);
        bool selectLibraryVersion (const juce::String&);
        bool selectLibrarySong (const juce::String&);
        bool selectLibraryCheck (const juce::String&);
        bool retryPresetSelection();
        bool retryCandidatePreparation();
        bool approveSampleRateConversion();
        bool requestRecovery();

        void observeTransport (std::int64_t hostPosition, bool positionValid,
                               bool playing) noexcept;
        void setContentObservationEnabled (bool enabled) noexcept;
        void setQueuedContentObservationEnabled (bool enabled) noexcept;
        void observeAInput (const juce::AudioBuffer<float>&,
                            std::int64_t hostPosition,
                            bool positionValid,
                            bool playing,
                            bool auditionAllowed, bool confirmAudible = true) noexcept;
        void confirmAOutput() noexcept { aAudibleConfirmations.fetch_add (1, std::memory_order_release); }
        bool selectB (double aIntegratedLoudness, double aMaximumTruePeakDbtp,
                      std::uint64_t queuedGeneration = 0, const juce::String& expectedPlaybackIdentity = {}) noexcept;
        std::uint64_t normalSelectionTicket() const noexcept { return normalSelectionGeneration.load (std::memory_order_acquire); }
        void selectA (bool allowFade = true) noexcept;
        bool hasOutputPath() const noexcept { return bSelected.load (std::memory_order_acquire) || returningToA() || normalAudible.load (std::memory_order_acquire) || blind.ongoing(); }
        bool canTransferOutputGate() const noexcept { return !bSelected.load (std::memory_order_acquire) && !blind.ongoing(); }
        bool returningToA() const noexcept { return normalReturnToken.load (std::memory_order_acquire); }
        bool outputSelected() const noexcept { return bSelected.load (std::memory_order_acquire); }
        bool startBlind (double, double) noexcept;
        bool approveBlindLowerAAndStart (double, double) noexcept;
        bool selectBlindStimulus (int) noexcept;
        bool answerBlind (int) noexcept;
        bool revealBlind() noexcept;
        void endBlind() noexcept;
        void suspendAudition() noexcept;
        bool renderSelectedB (juce::AudioBuffer<float>&, std::int64_t hostPosition,
                              bool positionValid, bool auditionAllowed = true) noexcept;
        bool renderSelectedB (juce::AudioBuffer<float>&, std::int64_t hostPosition,
                              bool positionValid, bool auditionAllowed,
                              bool normalReturnAllowed, bool normalTarget = true) noexcept;
        void loseAudibleConfirmation() noexcept;
        // 選んでいるあいだの追従（メッセージスレッドから 1 秒ごと）。history は 10 Hz のメーター履歴、
        // aSessionPeakDbtp は A のセッションの max TP。V と、追従にした役（B）だけが動き、C は固定。
        RematchResult rematch (double aLoudness, double aSessionPeakDbtp) noexcept; // C の MATCH をもう一度
        VersionIdentity identifyVersions (const KirinFingerprint&, std::int64_t endTick); // （メッセージスレッド）
        TrackingAction followSelection (const std::vector<KirinMeterHistoryEntry>& history,
                                        double aSessionPeakDbtp) noexcept;
        void setTrackingEnabled (bool enabled) noexcept { trackingEnabled.store (enabled, std::memory_order_release); }
        // 承認して A を下げている量（0 以下）。MATCH・追従・やり直しの上限はこの量を足した後の音で見る。
        void setHeldAttenuation (double db) noexcept { heldAttenuationDb.store (std::min (0.0, db), std::memory_order_release); }
        // B（REF）の役は B セットの曲だけを鳴らす。曲を選ぶまでは何も準備しない（C の Preset に落ちない）。
        void setSongsOnly (bool enabled) noexcept { songsOnly.store (enabled, std::memory_order_release); notify(); }
        bool trackingAudible() const noexcept
        { return trackingEnabled.load (std::memory_order_acquire) && bSelected.load (std::memory_order_acquire) && ! blind.ongoing(); }
        // MATCH の A 側の窓の長さ（10 Hz のブロック数）。追従する役は 10 秒、固定する役（C）は Cue と同じ長さ。
        int matchWindowBlocks() const;
        // 停止・シークで A に戻った選択を、同じ音（playback identity）・同じ gain のまま戻す。
        // 利用者が A を押す・別の音にする・試聴を止められたときは忘れる（forgetHeldSelection）。
        bool resumeHeld (std::uint64_t selectionGeneration, const juce::String& playbackIdentity) noexcept;
        bool hasHeldSelection() const;
        std::uint64_t requestedGeneration() const; // 今の選択の世代（状態の selectionGeneration と比べる）
        juce::String heldPlaybackIdentity() const;
        void forgetHeldSelection();
        // A へ切る道（ライブラリが公開を引っ込めた・音源が確かめられない）で戻す控えを消した。比べる制御が一度だけ読み、
        // 鳴っていた役を「音源が変わった」で止める（同じ音が戻っても勝手に鳴り直さない）。
        bool takeHeldWithdrawn() noexcept { return heldWithdrawn.exchange (false, std::memory_order_acq_rel); }

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
            std::uint64_t sessionSequence = 0;
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
            TrackingState tracking = TrackingState::none;
            double anchorGainDb = 0.0; // 利用者の MATCH の gain。追従はここから ±6 dB まで（戻すときも同じ値）
            double peakShortfallDb = 0.0;  // 上限まで上げて止めた量（0.5 dB 以下）
            bool valid = false;
        };

        void run() override;
        void applyConfiguration (const Configuration&);
        void refreshWorkspace (const Configuration&, std::int64_t nowMs);
        void serviceBlindPreparation (const Configuration&, const RuntimeCandidate&, const RuntimeCue&,
                                      const std::shared_ptr<const RuntimeSource>&, Snapshot&);
        void publish (Snapshot);
        void publishReady (Snapshot, std::shared_ptr<const RuntimeSource>, const RuntimeCue&);
        void publishApprovalRequired (Snapshot, const juce::String& approvalKey,
            std::shared_ptr<const RuntimeSource>, const RuntimeCue&);
        void publishLocked (Snapshot);
        bool requestSelection (const juce::String& kind, const juce::String& id);
        std::int64_t mappedSourcePosition (std::int64_t hostPosition) const noexcept;
        // DAW の位置に合わせない曲（別の曲の B・C）は、選んだときの DAW の位置を起点に Cue の頭から進む。押したとき・
        // 自動で戻すときに今の位置がその Cue の外（選んだ後に頭へ戻した・Cue を過ぎた）なら、今の位置を起点に Cue の
        // 頭から鳴らし直す。DAW の位置に合わせる曲（同じ Work・V）は置き直さない（位置を動かすのは利用者）。
        bool restartsAtCueStart() const noexcept;
        // Audio Thread。Cue の頭から鳴らし直す役が鳴っているあいだに Cue の外へ出た（Cue の終わり・DAW のループ）とき
        // の位置：選んだときの起点から Cue を周回した位置（終わりの次は頭）。公開を引っ込めず、試聴を作り直さない。
        std::int64_t loopedCuePosition (std::int64_t hostPosition) const noexcept;
        bool restartCueAtPlayhead (std::int64_t hostPosition) noexcept;
        bool prepareReferenceGain (double aIntegratedLoudness,
                                   double aMaximumTruePeakDbtp,
                                   std::uint64_t selectionGeneration, const juce::String& expectedPlaybackIdentity) noexcept;
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
        bool requestLibraryRecovery();
        void serviceLibraryRecovery();
        void serviceRecoveryAcknowledgement();
        void servicePresetSelectionAcknowledgement();
        void serviceCandidatePreparationAcknowledgement();
        void serviceDeferredAudioThreadActions();
        void serviceOutputRetirement();
        void revokeAuditionPublication() noexcept;
        // 鳴っている（A へ戻るフェード中を含む）役の選択を替える。先に公開を取り消すと Audio Thread が
        // フェードを掛けずに A を返す（ぷつっと切れる）ので、フェードが終わってから作業スレッドが取り消す。
        // stateLock を持って呼ぶ。待つのは最長 revokeFadeLimitMs。
        void revokeAfterFadeLocked() noexcept;
        bool deferredRevokeWaiting() noexcept; // 作業スレッド：フェードの終わりを待っているあいだ true
        void failClosedToA() noexcept;
        void invalidateBlind() noexcept;
        void failClosedToAFromAudioThread() noexcept;
        void invalidateBlindFromAudioThread() noexcept;

        const juce::File root;
        const bool versionComparison;
        const SelectionGate selectionGate;
        RuntimeV2Repository repository;
        RuntimeABindingRepository aBindingRepository;
        RuntimeACapture aCapture;
        RuntimeV2SourceRepository sourceRepository;
        RuntimeV2SourceCache sourceCache;
        RuntimeV2MeasurementRepository measurementRepository;
        RuntimeV2AlignmentRepository alignmentRepository;
        RuntimeV2ProfileRepository profileRepository;
        RuntimeV2PresentationRepository presentationRepository;
        RecoveryTransport recoveryTransport;
        PresetSelectionTransport presetSelectionTransport;
        CandidatePreparationTransport candidatePreparationTransport;
        PresetAdoptionTransport presetAdoptionTransport;
        RuntimeEventTransport eventTransport;
        AudioPages pages;
        RuntimeV2Blind blind;
        mutable juce::CriticalSection stateLock;
        juce::CriticalSection outputGateLock;
        Configuration requestedConfiguration;
        RequestedSelection requestedSelection;
        std::uint64_t appliedConfigurationGeneration = 0;
        std::atomic<std::uint64_t> appliedSelectionGeneration { 0 }; // 作業スレッドが書き、公開の状態に写す
        std::shared_ptr<const RuntimeWorkspace> workspace;
        std::shared_ptr<const RuntimeLibraryPreparation> libraryPreparation; // stateLock：Kirin OS の準備の状態
        VersionIdentifier versionIdentifier; // Version の指紋（作業スレッドが読み、メッセージスレッドが照合する）
        std::optional<RuntimeABinding> activeABinding;
        RuntimeFiles activeRuntimeFiles;
        std::shared_ptr<const RuntimeSource> workerSource;
        std::shared_ptr<const RuntimeSource> publishedSource;
        // Non-RT visual evidence only. This never grants ready/playback authority.
        std::shared_ptr<const RuntimeSource> approvalVisualSource;
        std::int64_t visualSourceCueStart = 0, visualSourceCueEnd = 0;
        juce::String activeSourceKey;
        juce::String activeMappingKey;
        juce::String activeContentMappingKey;
        juce::String activePublishedSelectionKey;
        juce::String pendingApprovalKey;
        juce::String blindContextKey;
        juce::String blindPreparationKey;
        juce::String legacyVersionLookupKey, legacyVersionChoice;
        CalibrationObservation calibrationObservation;
        juce::String activePresetAdoptionKey;
        Snapshot currentSnapshot;
        PreparedNormalSelection preparedNormalSelection;
        struct HeldSelection
        {
            juce::String playbackIdentity;
            PreparedNormalSelection facts;  // 最後に掛けていた gain と、その時の値（追従で動いた後の値）
            bool valid = false;
        } heldSelection; // stateLock
        double trackingAnchorDb = 0.0; // stateLock：鳴っている選択の MATCH の gain（追従の幅の中心）
        std::atomic<bool> heldWithdrawn { false };
        void holdCurrentGainLocked() noexcept;
        // MATCH の結果（失敗・承認の下げ幅）を書くのはこの 2 つだけ。試みの始めに前の失敗を必ず消す（成功しても前の
        // 失敗が残ると、古い量の承認が出る）。stateLock を持って呼ぶ。
        void beginMatchLocked() noexcept;
        void failMatchLocked (MatchFailure, double neededAttenuationDb) noexcept;
        // 決めた gain を掛け、状態の値（A・調整後・差）を合わせる。stateLock を持って呼ぶ。
        void applyMatchedGainLocked (double gainDb, double aLoudness, double aPeakDbtp,
                                     double sourceLoudness, double sourcePeakDbtp) noexcept;
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
        std::optional<CandidatePreparationRequest> pendingCandidatePreparationRequest;
        std::optional<CandidatePreparationTarget> failedCandidatePreparationTarget;
        std::int64_t recoveryStatusExpiresAtMs = 0;
        std::int64_t presetSelectionWaitingSinceMs = 0;
        std::int64_t presetSelectionStatusExpiresAtMs = 0;
        std::int64_t candidatePreparationWaitingSinceMs = 0;
        std::int64_t candidatePreparationStatusExpiresAtMs = 0;
        juce::File pendingLibraryOpen;
        std::int64_t libraryOpenRequestedAt = 0;
        std::atomic<bool> libraryReceived { false }, libraryOnline { false };
        std::atomic<bool> ready { false };
        std::atomic<bool> bSelected { false };
        std::atomic<std::uint64_t> normalReturnToken { 0 };
        std::atomic<bool> normalAudible { false };
        std::atomic<float> normalFadeStep { 1.0f / 240.0f };
        std::array<std::array<float, 8192>, 2> normalLiveA {};
        std::uint64_t rtNormalEpoch = 0;
        float rtNormalBlend = 0.0f; // Audio-thread owned.
        void setContentObservationDemand (unsigned bit, bool enabled) noexcept;
        std::atomic<unsigned> contentObservationDemands { 0 }; // 1: view/capture, 2: queued B.
        std::atomic<bool> contentRefreshRequested { false };
        std::atomic<float> bLinearGain { 1.0f };
        std::atomic<bool> trackingEnabled { false };
        std::atomic<double> heldAttenuationDb { 0.0 };
        std::atomic<bool> songsOnly { false };
        std::atomic<int> trackingRampFrames { 2400 };
        TrackingGainRamp rtTrackingRamp; // Audio-thread owned.
        std::atomic<std::uint64_t> auditionEpoch { 1 };
        std::atomic<std::uint64_t> activeAuditionEpoch { 0 };
        std::atomic<std::uint64_t> normalSelectionGeneration { 1 };
        std::atomic<std::int64_t> latestHostPosition { 0 };
        std::atomic<bool> latestPositionValid { false };
        std::atomic<bool> latestPlaying { false };
        std::atomic<std::uint64_t> transportHeartbeat { 0 };
        std::atomic<std::int64_t> cueStart { 0 };
        std::atomic<std::int64_t> cueEnd { 0 };
        std::atomic<bool> cueLoops { false };
        std::atomic<bool> sampleLocked { false };
        std::atomic<std::uint64_t> mappingGeneration { 0 };
        std::atomic<std::uint64_t> bAudibleConfirmations { 0 };
        std::atomic<std::uint64_t> aAudibleConfirmations { 0 };
        std::atomic<std::int64_t> bHostAnchor { 0 };
        std::atomic<std::int64_t> bSourceAnchor { 0 };
        // 対応づけ（cueStart・cueEnd・cueLoops・sampleLocked・起点）を書くのは作業スレッドとメッセージスレッド
        // （restartCueAtPlayhead）。書き手どうしをこの鍵でそろえる。音声スレッドは鍵を取らず mappingGeneration で読む。
        juce::CriticalSection mappingWriteLock;
        std::atomic<std::uint64_t> nextOutputGateToken { 1 };
        std::atomic<std::uint64_t> activeOutputGateToken { 0 };
        std::atomic<std::uint64_t> normalGateReleasePendingToken { 0 };
        std::atomic<std::uint64_t> blindGateReleasePendingToken { 0 };
        std::atomic<bool> auditionReturnPending { false };
        std::atomic<bool> revokeAfterFade { false };
        std::atomic<std::uint32_t> revokeFadeStartedMs { 0 };
        DeferredControl outputRetirement;
    };
}
