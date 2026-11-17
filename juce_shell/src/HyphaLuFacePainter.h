#pragma once

#include "HyphaHybridVuPainter.h"
#include "HyphaLuNeedle.h"

#include <vector>

// The LU face of the Hybrid VU. The box, header, True Peak rails, clip lamps and the three value
// cells are the VU's own (HyphaHybridVuPainter); only the dial changes: one LU scale on the left
// hub with up to three needles, and what the 0 is on the right.
namespace hypha::hybrid_vu::lu_face
{
enum class Needle { own, pre, samePlace };

struct Frame
{
    const State& vu;
    lu_needle::Facts facts;
    lu_needle::Window window = lu_needle::Window::momentary;
    lu_needle::Reading reading;
    // Needle positions on the scale (0 to 1), already eased.
    float own = 0.0f, pre = 0.0f, samePlace = 0.0f;
};

// One piece of text the face drew, for the contract tests: where, and at what height.
struct TextBox
{
    juce::String text;
    juce::Rectangle<float> area;
    float fontHeight = 0.0f;
};

// The scale that never moves: arc, ticks and figures. Drawn over hybrid_vu::paintStatic.
void paintScale (juce::Graphics&, juce::Rectangle<int>, const State&, std::vector<TextBox>* = nullptr);
// The set's band, the needles, the dial readout and the right-hand text.
void paintLive (juce::Graphics&, juce::Rectangle<int>, const Frame&, std::vector<TextBox>* = nullptr);
// Everything at once, for offscreen rendering.
void paint (juce::Graphics&, juce::Rectangle<int>, const Frame&, std::vector<TextBox>* = nullptr);

// The part of the view the needles and the dial readout can touch.
juce::Rectangle<int> dialBounds (juce::Rectangle<int>);
// A point on a needle: reach 0 is the hub, 1 the arc.
juce::Point<float> needlePoint (juce::Rectangle<int>, float normalized, float reach);
juce::Colour needleColour (Needle) noexcept;
}
