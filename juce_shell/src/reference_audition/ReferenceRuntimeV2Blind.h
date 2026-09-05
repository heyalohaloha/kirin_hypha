#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "ReferenceAuditionController.h"
#include "ReferenceRuntimeACapture.h"
#include "ReferenceRuntimeV2Source.h"

namespace hypha::reference_audition
{
    struct RuntimeV2BlindGainPlan
    {
        double aGainDb = 0.0;
        double bGainDb = 0.0;
        double preservedPeakCeilingDbtp = -1.0;
        double requiredAAttenuationDb = 0.0;
        bool lowerAApprovalRequired = false;
    };

    RuntimeV2BlindGainPlan planRuntimeV2BlindGain (
        double pairedLoudnessDeltaDb, double aCueTruePeakDbtp,
        double bCueTruePeakDbtp) noexcept;

    struct RuntimeV2BlindCommitment
    {
        bool stimulusOneIsB = false;
        juce::String trialId;
        juce::String nonceHex;
        juce::String commitmentSha256;
    };

    RuntimeV2BlindCommitment createRuntimeV2BlindCommitment();

    struct RuntimeV2BlindSnapshot
    {
        BlindPhase phase = BlindPhase::inactive;
        bool eligible = false;
        bool lowerAApprovalRequired = false;
        double requiredAAttenuationDb = 0.0;
        int activeStimulus = 0;
        int pendingStimulus = 0;
        int answeredStimulus = 0;
        int revealedStimulusOneSide = -1;
        double aGainDb = 0.0;
        double bGainDb = 0.0;
        double aCueTruePeakDbtp = 0.0;
        double bCueTruePeakDbtp = 0.0;
        double pairedLoudnessDeltaDb = 0.0;
        double preservedPeakCeilingDbtp = -1.0;
        std::uint64_t pairedBlockCount = 0;
        std::int64_t aStartSample = 0;
        std::int64_t aEndSample = 0;
        std::int64_t bStartSample = 0;
        std::int64_t bEndSample = 0;
        std::int64_t aSampleRateHz = 0;
        std::int64_t bSampleRateHz = 0;
        int channels = 0;
        juce::String dawRevisionId;
        juce::String aCuePcmSha256;
        juce::String bCuePcmSha256;
        std::uint64_t stimulusOneAudibleFrames = 0;
        std::uint64_t stimulusTwoAudibleFrames = 0;
        std::uint64_t stimulusOneConfirmedSwitches = 0;
        std::uint64_t stimulusTwoConfirmedSwitches = 0;
        std::uint64_t firstCallbackSequence = 0;
        std::uint64_t lastCallbackSequence = 0;
        juce::String trialId;
        juce::String assignmentCommitmentSha256;
        juce::String revealedNonceHex;
        juce::String rejectionCode;
    };

    class RuntimeV2Blind final
    {
    public:
        RuntimeV2Blind() = default;

        void prepare (const std::shared_ptr<const RuntimeACaptureAudio>&,
                      const RuntimeCandidate&, const RuntimeCue&,
                      const std::shared_ptr<const RuntimeSource>&,
                      bool sampleRateConversionApproved);
        void invalidate() noexcept;
        void clear() noexcept;

        bool start (bool approveLowerA = false) noexcept;
        bool requestStimulus (int) noexcept;
        bool answer (int) noexcept;
        bool reveal() noexcept;
        void end() noexcept;
        void loseAudibleConfirmation() noexcept;
        bool render (juce::AudioBuffer<float>&, std::int64_t hostPosition,
                     bool positionValid) noexcept;
        bool renderInvalidatedA (juce::AudioBuffer<float>&,
                                 bool auditionAllowed) noexcept;
        RuntimeV2BlindSnapshot snapshot() const;
        bool ongoing() const noexcept;

    private:
        enum Lifecycle : int
        {
            unavailable = 0,
            preparing = 1,
            prepared = 2,
            approvalRequired = 3,
            active = 4,
            revealed = 5,
            invalidated = 6,
        };

        bool enterPreparation() noexcept;
        void resetSession() noexcept;
        int sideForStimulus (int stimulus) const noexcept;
        static BlindPhase publicPhase (int lifecycle) noexcept;

        std::vector<float> frozenA;
        std::vector<float> frozenB;
        std::int64_t aStartSample = 0;
        std::int64_t frameCount = 0;
        int channels = 0;
        int sampleRateHz = 0;
        bool loopEnabled = false;
        double aGainDb = 0.0;
        double bGainDb = 0.0;
        double requiredAAttenuationDb = 0.0;
        double aCueTruePeakDbtp = 0.0;
        double bCueTruePeakDbtp = 0.0;
        double pairedLoudnessDeltaDb = 0.0;
        double preservedPeakCeilingDbtp = -1.0;
        std::uint64_t pairedBlockCount = 0;
        std::int64_t bStartSample = 0;
        std::int64_t bEndSample = 0;
        int bSampleRateHz = 0;
        juce::String dawRevisionId;
        juce::String aCuePcmSha256;
        juce::String bCuePcmSha256;
        juce::String rejectionCode;
        juce::String trialId;
        juce::String assignmentCommitmentSha256;
        juce::String assignmentNonceHex;

        std::atomic<int> lifecycle { unavailable };
        std::atomic<int> callbacksInFlight { 0 };
        mutable std::atomic<int> snapshotReadersInFlight { 0 };
        std::atomic<bool> stimulusOneIsB { false };
        std::atomic<int> requestedStimulus { 0 };
        std::atomic<int> activeStimulus { 0 };
        std::atomic<int> answeredStimulus { 0 };
        std::atomic<std::uint64_t> requestSequence { 0 };
        std::atomic<std::uint64_t> confirmedSequence { 0 };
        std::atomic<std::uint64_t> callbackSequence { 0 };
        std::atomic<std::uint64_t> firstCallbackSequence { 0 };
        std::atomic<std::uint64_t> lastCallbackSequence { 0 };
        std::atomic<std::uint64_t> stimulusOneFrames { 0 };
        std::atomic<std::uint64_t> stimulusTwoFrames { 0 };
        std::atomic<std::uint64_t> stimulusOneSwitches { 0 };
        std::atomic<std::uint64_t> stimulusTwoSwitches { 0 };
    };
}
