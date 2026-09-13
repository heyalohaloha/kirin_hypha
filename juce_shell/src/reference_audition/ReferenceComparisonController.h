#pragma once
#include "ReferenceRuntimeV2Controller.h"

namespace hypha::reference_audition
{
// A is the original DAW input. B and C own separate prepared choices, while one
// shared gate admits only the explicitly selected output path.
class ReferenceComparisonController final
{
public:
    using SelectionGate = RuntimeV2Controller::SelectionGate;
    explicit ReferenceComparisonController (juce::File, SelectionGate = {});
    ~ReferenceComparisonController();
    void configure (RuntimeIdentity, double, int);
    Snapshot snapshot() const;
    bool selectVersion (const juce::String&);
    bool selectPreset (const juce::String&);
    bool selectCheck (const juce::String&);
    bool selectCandidate (const juce::String&);
    bool selectCue (const juce::String&);
    bool retryPresetSelection();
    bool retryCandidatePreparation();
    bool approveSampleRateConversion();
    bool requestRecovery();
    bool selectB (double, double) noexcept;
    bool selectC (double, double) noexcept;
    void selectA() noexcept;
    bool startBlind (double, double) noexcept;
    bool approveBlindLowerAAndStart (double, double) noexcept;
    bool selectBlindStimulus (int) noexcept;
    bool answerBlind (int) noexcept;
    bool revealBlind() noexcept;
    void endBlind() noexcept;
    void suspendAudition() noexcept;
    void observeTransport (std::int64_t, bool, bool) noexcept;
    void observeAInput (const juce::AudioBuffer<float>&, std::int64_t, bool, bool, bool) noexcept;
    bool renderSelectedB (juce::AudioBuffer<float>&, std::int64_t, bool, bool, bool) noexcept;

private:
    bool admit (int, bool);
    RuntimeV2Controller& viewed() noexcept;
    bool trialActive() const;
    SelectionGate gate;
    juce::CriticalSection gateLock;
    int gateOwner = 0;
    mutable juce::CriticalSection selectionLock;
    juce::String versionId, receiverId;
    std::atomic<int> viewedSlot { 2 };
    bool rtPlaying = false, rtInputAllowed = false;
    RuntimeV2Controller version, check;
};
}
