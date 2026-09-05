#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include <juce_core/juce_core.h>

#include "ReferenceAudioPages.h"
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
    };

    struct Snapshot
    {
        RuntimeState state = RuntimeState::disconnected;
        juce::String title;
        juce::String sourceKind;
        juce::String rejectionCode;
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
        bool bSelected = false;
        bool transportPlaying = false;
        bool transportPositionValid = false;
        bool auditionBuffered = false;
        bool blindEligible = false;
        BlindPhase blindPhase = BlindPhase::inactive;
        int activeBlindStimulus = 0;
        int pendingBlindStimulus = 0;
        int answeredBlindStimulus = 0;
        bool blindStimulusOneHeard = false;
        bool blindStimulusTwoHeard = false;
        juce::String blindReveal;
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
        std::shared_ptr<const RuntimeDetailedMeasurement> detailedMeasurement;
        std::vector<std::shared_ptr<const RuntimeProfile>> profiles;
        bool sampleRateApprovalRequired = false;
        std::int64_t sourceSampleRateHz = 0;
        std::int64_t hostSampleRateHz = 0;
        bool measurementAvailable = false;
        bool alignmentPrepared = false;
        bool aBindingAvailable = false;
        juce::String aRecordingId;
        bool aCaptureAvailable = false;
        juce::String recoveryStatus;
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
    };
}
