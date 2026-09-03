#pragma once

#include <atomic>
#include <memory>

#include <juce_core/juce_core.h>

#include "ReferenceAudioPages.h"
#include "ReferenceAuditionLease.h"
#include "ReferenceAuditionRepository.h"

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
        bool bSelected = false;
    };

    class Controller final : private juce::Thread
    {
    public:
        explicit Controller (juce::File transportRootIn = Repository::transportRoot());
        ~Controller() override;

        void configure (RuntimeIdentity, double hostSampleRate, int hostChannels);
        void disconnect();
        Snapshot snapshot() const;

        void observeTransport (std::int64_t hostPosition, bool positionValid,
                               bool playing) noexcept;
        bool selectB (double aIntegratedLoudness, double aMaximumTruePeakDbtp) noexcept;
        void selectA() noexcept;
        bool renderSelectedB (juce::AudioBuffer<float>&, std::int64_t hostPosition,
                              bool positionValid) noexcept;

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

        const juce::File root;
        Repository repository;
        AudioPages pages;
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
    };
}
