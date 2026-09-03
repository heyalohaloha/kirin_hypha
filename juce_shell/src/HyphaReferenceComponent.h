#pragma once

#include <cmath>
#include <functional>
#include <limits>

#include <juce_gui_basics/juce_gui_basics.h>

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
};

inline bool canSelectB (const State& state) noexcept
{
    return state.readiness == Readiness::ready
        && state.aAvailable
        && std::isfinite (state.aIntegratedLoudness)
        && std::isfinite (state.aMaximumTruePeakDbtp);
}

class Component final : public juce::Component
{
public:
    Component();

    std::function<void()> onSelectA;
    std::function<void()> onSelectB;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Component)
};
}
