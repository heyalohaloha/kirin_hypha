#pragma once

#include <cmath>
#include <functional>
#include <limits>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaOsAccess.h"

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
    bool bSelected = false;
    bool auditionBuffered = false;
    os_access::State osAccess = os_access::State::unowned;
    BlindPhase blindPhase = BlindPhase::unavailable;
    int activeBlindStimulus = 0;
    int pendingBlindStimulus = 0;
    juce::String blindReveal;
};

inline bool canSelectB (const State& state) noexcept
{
    return os_access::featureReady (state.osAccess)
        && state.readiness == Readiness::ready && state.auditionBuffered
        && state.aAvailable
        && std::isfinite (state.aIntegratedLoudness)
        && std::isfinite (state.aMaximumTruePeakDbtp);
}

inline bool canStartBlind (const State& state) noexcept
{
    return (state.blindPhase == BlindPhase::available
            || state.blindPhase == BlindPhase::invalidated)
        && canSelectB (state);
}

class Component final : public juce::Component
{
public:
    Component();

    std::function<void()> onSelectA;
    std::function<void()> onSelectB;
    std::function<void()> onStartBlind;
    std::function<void(int)> onSelectBlindStimulus;
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
    SideButton aButton { "A" };
    SideButton bButton { "B" };
    SideButton blindButton { "BLIND" };
    SideButton oneButton { "1" };
    SideButton twoButton { "2" };
    SideButton revealButton { "REVEAL" };
    SideButton endBlindButton { "END" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Component)
};
}
