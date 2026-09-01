#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaObservatoryContract.h"

namespace hypha::observatory_world
{
struct State
{
    observatory::Role role = observatory::Role::post;
    observatory::Domain domain = observatory::Domain::level;
    observatory::Density density = observatory::Density::compact;
    bool active = false;
    bool paired = false;
    bool guidePresent = false;
    bool capture = false;
    float energy = 0.0f;
    float direction = 0.0f;
};

constexpr float backdropOpacity (const State& state) noexcept
{
    const float density = state.density == observatory::Density::compact ? 0.48f
                        : state.density == observatory::Density::focused ? 0.58f
                        : state.density == observatory::Density::standard ? 0.68f : 0.78f;
    const float role = state.role == observatory::Role::pre ? 0.72f : 1.0f;
    const float signal = state.active ? 1.0f : 0.48f;
    const float capture = state.capture ? 1.08f : 1.0f;
    return density * role * signal * capture;
}

class Backdrop
{
public:
    Backdrop();
    void draw (juce::Graphics&, juce::Rectangle<int>, const State&) const;
    void drawHyphaSpecimen (juce::Graphics&, juce::Rectangle<int>, const State&) const;
    bool isValid() const noexcept { return image.isValid() && hyphaSpecimen.isValid(); }

private:
    juce::Image image;
    juce::Image hyphaSpecimen;
};

void paintDomainBed (juce::Graphics&, juce::Rectangle<int>, const State&);
void paintPlateFrame (juce::Graphics&, juce::Rectangle<int>, const State&);
void paintPairRoot (juce::Graphics&, juce::Rectangle<int>, const State&,
                    juce::Colour connectionColour);
void paintGuideRoot (juce::Graphics&, juce::Rectangle<int>, const State&);
}
