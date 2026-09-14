#pragma once
#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceVisualObservation.h"
#include "ReferenceACaptureSession.h"
#include "ReferenceACaptureProjection.h"

namespace hypha::reference_audition
{
// A is the original DAW input. B and C own separate prepared choices, while one
// shared gate admits only the explicitly selected output path.
class ReferenceComparisonController final
{
public:
    using SelectionGate = RuntimeV2Controller::SelectionGate;
    explicit ReferenceComparisonController (juce::File, SelectionGate = {}, SelectionGate = {}, SelectionGate = {});
    ~ReferenceComparisonController();
    void configure (RuntimeIdentity, double, int);
    void setPresented (bool active) noexcept;
    Snapshot snapshot() const;
    ReferenceComparisonSettings savedSettings() const;
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
    void observeAInput (const juce::AudioBuffer<float>&, std::int64_t, bool, bool, bool, int clock = 0, std::optional<bool> captureAllowed = {}, CaptureClockSignature = {}) noexcept;
    bool renderSelectedB (juce::AudioBuffer<float>&, std::int64_t, bool, bool, bool) noexcept;

private:
    bool admit (int, bool);
    bool admitCapture(bool);
    bool beginBlindGuard();
    void endBlindGuard();
    void refreshObservation();
    ACaptureReceipt captureReceipt() const;
    RuntimeV2Controller& viewed() noexcept;
    bool trialActive() const;
    SelectionGate gate, captureGate, blindCaptureGate;
    bool captureOwned=false,blindGuardOwned=false;
    std::atomic<bool> presented{false};
    juce::CriticalSection gateLock;
    bool closing = false;
    int gateOwners = 0; // Bit mask retains one external admission across overlapping tails.
    mutable juce::CriticalSection selectionLock;
    juce::String versionId, receiverId;
    std::optional<ReferenceComparisonSettings> pendingSettings;
    bool configured = false;
    std::atomic<int> viewedSlot { 2 }, normalOutputSlot { 0 };
    std::atomic<bool> versionChosen { false };
    bool rtPlaying = false, rtInputAllowed = false;
    juce::AudioBuffer<float> bScratch { 2, 8192 }, cScratch { 2, 8192 };
    RuntimeV2Controller version, check;
    VisualObservation visual;
    ACaptureSession capture;
    ACaptureProjection captureProjection;
    std::shared_ptr<VisualPreferences> visualPreferences = std::make_shared<VisualPreferences>();
};
}
