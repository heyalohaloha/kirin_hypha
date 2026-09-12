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
    observatory::ConnectionState connection = observatory::ConnectionState::unpaired;
    bool guidePresent = false;
    bool capture = false;
    bool jungle = false;
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
    const float jungle = state.jungle ? 1.08f : 1.0f;
    return density * role * signal * capture * jungle;
}

class Backdrop
{
public:
    Backdrop();
    void draw (juce::Graphics&, juce::Rectangle<int>, const State&) const;
    void drawLevelCorners (juce::Graphics&, juce::Rectangle<int>, const State&) const;
    void drawHyphaSpecimen (juce::Graphics&, juce::Rectangle<int>, const State&) const;
    void drawDomainBed (juce::Graphics&, juce::Rectangle<int>, const State&) const;
    bool isValid() const noexcept
    {
        return image.isValid() && levelCorners.isValid() && hyphaSpecimen.isValid();
    }

private:
    juce::Image image;
    juce::Image levelCorners;
    juce::Image hyphaSpecimen;
    // Only the immutable texture is cached; state-dependent opacity remains live.
    mutable juce::Image scaledBackdrop;
    mutable juce::Point<int> scaledBackdropLogicalSize;
    mutable float scaledBackdropPixelScale = 0.0f;
    mutable juce::Image domainBed;
    mutable juce::Point<int> domainBedSize;
    mutable float domainBedPixelScale = 0.0f;
    mutable State domainBedState;
};

juce::Rectangle<float> aspectFillSourceBounds (int sourceWidth, int sourceHeight,
                                                int targetWidth, int targetHeight) noexcept;
void drawAspectFill (juce::Graphics&, const juce::Image&, juce::Rectangle<int> target);

void paintDomainBed (juce::Graphics&, juce::Rectangle<int>, const State&);
void paintPlateFrame (juce::Graphics&, juce::Rectangle<int>, const State&);
void paintPairRoot (juce::Graphics&, juce::Rectangle<int>, const State&,
                    juce::Colour connectionColour);
void paintHyphaAperture (juce::Graphics&, juce::Rectangle<int>, const State&,
                         juce::Colour connectionColour);
void paintGuideRoot (juce::Graphics&, juce::Rectangle<int>, const State&);
}
