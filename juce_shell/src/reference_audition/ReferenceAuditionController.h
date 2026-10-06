#pragma once

#include <map>
#include <set>
#include "ReferencePendingAudition.h"
#include "ReferenceTrackingState.h"

#include <atomic>
#include <functional>
#include <limits>
#include <memory>

#include <juce_core/juce_core.h>

#include "ReferenceAudioPages.h"
#include "ReferenceCuePart.h"
#include "ReferenceDeferredControl.h"
#include "ReferenceVisualTimeline.h"
#include "ReferenceVisualPreferences.h"
#include "ReferenceBlindSession.h"
#include "ReferenceAuditionLease.h"
#include "ReferenceAuditionRepository.h"
#include "ReferenceRuntimeV2Measurement.h"
#include "ReferenceRuntimeV2Profile.h"

namespace hypha::reference_audition
{
    enum class RuntimeState
    {
        disconnected,
        waiting,
        verifying,
        ready,
        rejected,
    };

    struct RuntimeSelectionOption
    {
        juce::String id;
        juce::String label;
        juce::String revisionId;
        bool requiresPreparation = false;
    };

    // Hypha に届いた B セット（順位順、最大 3）と、その曲（B の一覧）。曲の id は選択の ID。
    struct RuntimeSongSetOption
    {
        juce::String id;
        juce::String name;
        int rank = 0;
        std::vector<RuntimeSelectionOption> songs;
        std::vector<RuntimeSongFacts> facts; // songs と同じ順。既定の Cue の Kirin OS の値
    };

    enum class MatchFailure { none, liveLevelUnavailable, sourceLevelUnavailable, ceilingExceeded };

    struct Snapshot
    {
        RuntimeState state = RuntimeState::disconnected;
        juce::String title;
        juce::String sourceKind;
        juce::String rejectionCode;
        MatchFailure matchFailure = MatchFailure::none;
        // 上限超え（ceilingExceeded）のとき、承認すれば合わせられる A の下げ幅（0 以下。2026-10-03）。
        double neededAttenuationDb = 0.0;
        double peakShortfallDb = 0.0;  // 上限まで上げて鳴らしていて、A に届かない量（0.5 dB 以下、0 なら合っている）
        std::uint64_t matchAttempt = 0;        // MATCH の試みの番号（押した・待たせた・やり直し）。知らせを一度にする鍵
        std::uint64_t matchFailureSerial = 0;  // 失敗と承認の下げ幅を作った・消したときだけ変わる番号
        // 承認して A（POST の出力全体）を下げている量（0 以下）。比較の制御が出す（役の値ではない）。
        double heldAttenuationDb = 0.0;
        // 窓に一度出した承認の申し出（役と失敗の番号）。窓を開き直しても出し直さない。比較の制御が出す。
        int lowerAOfferShownSlot = 0;
        std::uint64_t lowerAOfferShownSerial = 0;
        juce::String playbackIdentity; // Worker-published, same complete condition used to revoke audio.
        std::uint64_t selectionGeneration = 0; // 作業スレッドがこの状態を出したときに反映していた選択の世代
        AlignmentMode alignmentMode = AlignmentMode::referenceCue;
        double sourceIntegratedLoudness = 0.0;
        double sourceMaximumTruePeakDbtp = 0.0;
        double aIntegratedLoudness = 0.0;
        double aMaximumTruePeakDbtp = 0.0;
        double appliedGainDb = 0.0;
        double adjustedBIntegratedLoudness = 0.0;
        double adjustedBMaximumTruePeakDbtp = 0.0;
        double loudnessDeltaBMinusA = 0.0;
        double truePeakDeltaBMinusA = 0.0;
        bool gainLimited = false;
        bool comparisonFallbackOriginal = false;
        TrackingState tracking = TrackingState::none;
        // 選んだ Cue の Kirin OS の値（ranges）。無ければ曲全体の値で合わせている（Kirin OS で測り直すと使う）。
        bool cueLevelAvailable = false;
        double cueIntegratedLoudness = std::numeric_limits<double>::quiet_NaN();
        double cueMaximumTruePeakDbtp = std::numeric_limits<double>::quiet_NaN();
        int cueWindowBlocks = 100;  // C の A 側の窓（10 Hz のブロック数）
        // C の画面。Cue の 64 帯域・4 帯域（Kirin OS の値、gain の前）、Cue の位置・ループと音源の長さ（秒）。
        std::shared_ptr<const KirinSpectrumWindow> cueSpectrum;
        CuePart cuePart = CuePart::unknown;  // Cue が曲のどの部分か（C の図の凡例）
        double cueStartSeconds = std::numeric_limits<double>::quiet_NaN(), cueEndSeconds = std::numeric_limits<double>::quiet_NaN();
        double sourceDurationSeconds = std::numeric_limits<double>::quiet_NaN();
        bool cueLoops = false;
        double cuePlayheadSeconds = std::numeric_limits<double>::quiet_NaN();
        bool bSelected = false;
        bool transportPlaying = false;
        bool transportPositionValid = false;
        bool auditionBuffered = false;
        bool auditionOutsideCue = false;
        bool blindEligible = false;
        BlindPhase blindPhase = BlindPhase::inactive;
        int activeBlindStimulus = 0;
        int pendingBlindStimulus = 0;
        int answeredBlindStimulus = 0;
        bool blindStimulusOneHeard = false;
        bool blindStimulusTwoHeard = false;
        juce::String blindReveal;
        bool blindStimulusOneIsComparison = false;  // 開示の後：1 が比べる側（V、1 曲だけの B）
        bool blindLowerAApprovalRequired = false;
        double blindRequiredAAttenuationDb = 0.0;
        juce::String presetId;
        juce::String checkId;
        juce::String candidateId;
        juce::String cueId;
        juce::String presetName;
        juce::String checkLabel;
        juce::String candidateName;
        juce::String cueLabel;
        juce::String comparisonMode;
        juce::String presentationLayout { "auto" };
        std::vector<juce::String> viewBindings;
        std::vector<RuntimeSelectionOption> presets;
        std::vector<RuntimeSelectionOption> checks;
        std::vector<RuntimeSelectionOption> candidates;
        std::vector<RuntimeSelectionOption> cues;
        std::vector<RuntimeSelectionOption> versions;
        std::vector<RuntimeSelectionOption> checkTargets;
        std::map<juce::String, std::vector<juce::String>> checkViewBindings; // CHECK SET の Check ごとの表示（V のタブ）
        std::set<juce::String> listeningChecks;  // 耳で聴き比べる Check（Kirin OS の audition_only）。図の代わりに案内を出す
        std::vector<RuntimeSongSetOption> songSets;
        juce::String songSetsIssue; // sets.json を読めなかった・一部を飛ばした理由（空なら無し）
        std::vector<RuntimeSkippedItem> librarySkipped; // Kirin OS の項目のうち受け付けずに外したもの（名前と理由）
        std::shared_ptr<const RuntimeLibraryPreparation> libraryPreparation; // Kirin OS の準備の状態（無ければ null）
        std::vector<RuntimeCheckSetRank> checkSetRanks; // Kirin OS で「Hypha に出す」順位を付けた CHECK セット
        std::shared_ptr<const Snapshot> checkSelection, versionSelection;
        juce::String selectedVersionId, migratedVersionChoice;
        bool versionAuto = false; // 選んでいる Version は AUTO が選んだ（AUTO が選び直せる）
        bool separateComparisons = false, versionReady = false, checkReady = false;
        int comparisonSlot = 2, audibleComparisonSlot = 0;
        bool versionArmable = false, checkArmable = false;
        // B（REF）。選んだ B SET と曲、その役の状態（referenceSelection）。
        std::shared_ptr<const Snapshot> referenceSelection;
        juce::String selectedSongSetId, selectedSongId;
        bool referenceReady = false, referenceArmable = false;
        PendingAuditionView pendingAudition;
        std::shared_ptr<const RuntimeDetailedMeasurement> detailedMeasurement;
        std::shared_ptr<const VisualTimeline> visualTimeline;
        double visualPositionSeconds = -1.0;
        std::shared_ptr<VisualPreferences> visualPreferences;
        std::vector<std::shared_ptr<const RuntimeProfile>> profiles;
        std::int64_t sourceSampleRateHz = 0;
        std::int64_t hostSampleRateHz = 0;
        bool measurementAvailable = false;
        bool alignmentPrepared = false;
        bool aBindingAvailable = false;
        juce::String aRecordingId;
        bool aCaptureAvailable = false;
        juce::String recoveryStatus;
        juce::String presetSelectionStatus;
        juce::String presetSelectionAction;
        juce::String presetSelectionTargetId;
        juce::String candidatePreparationStatus;
        juce::String candidatePreparationAction;
        juce::String candidatePreparationTargetId;
        bool libraryReceived = false;
        bool osOnline = false;
        std::int64_t manifestRevision = 0;
    };

    class Controller final : private juce::Thread
    {
    public:
        using SelectionGate = std::function<bool(bool)>;
        using RandomBit = std::function<bool()>;

        explicit Controller (juce::File transportRootIn = Repository::transportRoot(),
                             SelectionGate = {}, RandomBit = {});
        ~Controller() override;

        void configure (RuntimeIdentity, double hostSampleRate, int hostChannels);
        void disconnect();
        Snapshot snapshot() const;

        void observeTransport (std::int64_t hostPosition, bool positionValid,
                               bool playing) noexcept;
        bool selectB (double aIntegratedLoudness, double aMaximumTruePeakDbtp) noexcept;
        void selectA() noexcept;
        bool startBlind (double aIntegratedLoudness, double aMaximumTruePeakDbtp) noexcept;
        bool selectBlindStimulus (int stimulus) noexcept;
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

        void run() override;
        void applyConfiguration (const Configuration&);
        void refreshPreparation (const Configuration&, std::int64_t nowMs);
        void publish (Snapshot);
        std::int64_t mappedSourcePosition (std::int64_t hostPosition) const noexcept;
        bool prepareReferenceGain (double aIntegratedLoudness,
                                   double aMaximumTruePeakDbtp) noexcept;
        bool activatePreparedB() noexcept;
        void selectAOutput() noexcept;
        void failClosedToA() noexcept;
        void invalidateBlind() noexcept;

        const juce::File root;
        const SelectionGate selectionGate;
        const RandomBit randomBit;
        Repository repository;
        AudioPages pages;
        BlindSession blindSession;
        mutable juce::CriticalSection configurationLock;
        Configuration requestedConfiguration;
        std::uint64_t appliedConfigurationGeneration = 0;
        mutable juce::CriticalSection snapshotLock;
        Snapshot currentSnapshot;
        Preparation activePreparation;
        SourceReceipt activeReceipt;
        RuntimeFiles activeRuntimeFiles;
        std::atomic<bool> ready { false };
        std::atomic<bool> bSelected { false };
        std::atomic<float> bLinearGain { 1.0f };
        std::atomic<int> alignmentMode { static_cast<int> (AlignmentMode::referenceCue) };
        std::atomic<std::int64_t> cueSourcePosition { 0 };
        std::atomic<std::int64_t> bHostAnchor { 0 };
        std::atomic<std::int64_t> latestHostPosition { 0 };
        std::atomic<bool> latestPositionValid { false };
        std::atomic<bool> latestPlaying { false };
        std::atomic<std::uint64_t> audioCallbackSequence { 0 };
        std::atomic<bool> gateReleasePending { false };
        DeferredControl outputRetirement;
    };
}
