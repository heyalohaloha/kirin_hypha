#pragma once

#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaOsAccess.h"
#include "reference_audition/ReferenceRuntimeV2Measurement.h"
#include "reference_audition/ReferenceRuntimeV2Profile.h"

namespace hypha::reference_ui
{
enum class Readiness
{
    disconnected,
    waiting,
    verifying,
    ready,
    rejected,
};

enum class BlindPhase
{
    unavailable,
    available,
    active,
    revealed,
    invalidated,
};

inline double unavailableValue() noexcept
{
    return std::numeric_limits<double>::quiet_NaN();
}

struct SelectionOption
{
    juce::String id;
    juce::String label;
};

struct State
{
    Readiness readiness = Readiness::disconnected;
    juce::String title;
    juce::String sourceLabel;
    juce::String status;
    juce::String alignmentLabel;
    double aIntegratedLoudness = unavailableValue();
    double aMaximumTruePeakDbtp = unavailableValue();
    double adjustedBIntegratedLoudness = unavailableValue();
    double adjustedBMaximumTruePeakDbtp = unavailableValue();
    double loudnessDeltaBMinusA = unavailableValue();
    double truePeakDeltaBMinusA = unavailableValue();
    double appliedGainDb = unavailableValue();
    bool aAvailable = false;
    bool gainLimited = false;
    bool comparisonFallbackOriginal = false;
    bool bSelected = false;
    bool auditionBuffered = false;
    os_access::State osAccess = os_access::State::unowned;
    BlindPhase blindPhase = BlindPhase::unavailable;
    int activeBlindStimulus = 0;
    int pendingBlindStimulus = 0;
    int answeredBlindStimulus = 0;
    bool blindStimulusOneHeard = false;
    bool blindStimulusTwoHeard = false;
    bool blindLowerAApprovalRequired = false;
    double blindRequiredAAttenuationDb = 0.0;
    juce::String blindReveal;
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
    std::vector<SelectionOption> presets;
    std::vector<SelectionOption> checks;
    std::vector<SelectionOption> candidates;
    std::vector<SelectionOption> cues;
    std::shared_ptr<const reference_audition::RuntimeDetailedMeasurement> detailedMeasurement;
    std::vector<std::shared_ptr<const reference_audition::RuntimeProfile>> profiles;
    std::vector<float> liveSpectrumDbfs;
    float liveSpectrumMinimumHz = 0.0f;
    float liveSpectrumMaximumHz = 0.0f;
    bool sampleRateApprovalRequired = false;
    std::int64_t sourceSampleRateHz = 0;
    std::int64_t hostSampleRateHz = 0;
    juce::String actionText;
};

inline bool canSelectB (const State& state) noexcept
{
    return os_access::featureReady (state.osAccess)
        && state.readiness == Readiness::ready && state.auditionBuffered
        && state.aAvailable;
}

inline bool canStartBlind (const State& state) noexcept
{
    return (state.blindPhase == BlindPhase::available
            || state.blindPhase == BlindPhase::invalidated)
        && ! state.blindLowerAApprovalRequired && canSelectB (state);
}

class Component final : public juce::Component
{
public:
    Component();

    std::function<void()> onSelectA;
    std::function<void()> onSelectB;
    std::function<void(const juce::String&)> onSelectPreset;
    std::function<void(const juce::String&)> onSelectCheck;
    std::function<void(const juce::String&)> onSelectCandidate;
    std::function<void(const juce::String&)> onSelectCue;
    std::function<void()> onAction;
    std::function<void()> onStartBlind;
    std::function<void(int)> onSelectBlindStimulus;
    std::function<void(int)> onAnswerBlind;
    std::function<void()> onRevealBlind;
    std::function<void()> onEndBlind;

    void setState (State);
    const State& state() const noexcept { return current; }
    bool detailedLayout() const noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SideButton final : public juce::TextButton
    {
    public:
        explicit SideButton (const juce::String& text);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    State current;
    juce::ComboBox presetBox;
    juce::ComboBox checkBox;
    juce::ComboBox candidateBox;
    juce::ComboBox cueBox;
    SideButton aButton { "A" };
    SideButton bButton { "B" };
    SideButton blindButton { "BLIND" };
    SideButton oneButton { "1" };
    SideButton twoButton { "2" };
    SideButton answerButton { "CHOOSE" };
    SideButton revealButton { "REVEAL" };
    SideButton endBlindButton { "END" };
    SideButton actionButton { "OPEN KIRIN OS" };

    void syncSelectionControl (juce::ComboBox&, const std::vector<SelectionOption>&,
                               const juce::String& selectedId);
    static juce::String selectedOptionId (const juce::ComboBox&,
                                          const std::vector<SelectionOption>&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Component)
};
}
