#pragma once

#include "HyphaLuFacePainter.h"
#include "kirin_hypha_ffi.h"

#include <functional>
#include <optional>
#include <vector>

namespace hypha::hybrid_vu
{
// The Hybrid VU's second face: LU against a Reference song from Kirin OS. It owns the two controls
// in the VU row (LU, and the M / S window), the choice, what Reference gave, and the needles.
// The face is offered only while Kirin OS's Reference can be read; otherwise the VU is unchanged
// and a saved choice is kept for the day it can be read again.
class LuFace final : private juce::Timer
{
public:
    explicit LuFace (juce::Component& owner);
    ~LuFace() override;

    std::function<void (bool luFace, lu_needle::Window)> onChoice; // the user's choice, to be saved
    std::function<void()> onLayoutChange;                          // the owner lays out and repaints

    void restore (bool luFace, lu_needle::Window);
    void setCommonFace (bool isLu, const juce::String& label, bool sharpOffered = true);
    std::function<void()> onFaceMenu;
    // POST: the plate that tells the song at 0 opens the choice of songs. Without it the plate only tells (PRE).
    std::function<void()> onReferenceMenu;
    juce::Button& referenceControl() noexcept;
    void setFacts (const lu_needle::Facts&);
    const lu_needle::Facts& facts() const noexcept { return current; }
    bool selected() const noexcept { return luSelected; }
    lu_needle::Window window() const noexcept { return chosenWindow; }
    bool available() const noexcept { return current.readable; }
    bool active() const noexcept { return luSelected && current.readable; }

    void hideControls();
    // Takes the controls' places from the left of the VU row.
    void layout (juce::Rectangle<int>& row, int buttonHeight, const presentation::Context&);
    void layoutReferenceStatus (juce::Rectangle<int>& row, int height, std::optional<bool> metricReady = std::nullopt,
                                int keptLeft = 0, const juce::String& metricSong = {});
    juce::String referenceStatus (std::optional<bool> metricReady = std::nullopt) const;
    juce::String referenceText (std::optional<bool> metricReady = std::nullopt, const juce::String& metricSong = {}) const; // the song at 0, else the status
    juce::String referenceSong (const juce::String& metricSong = {}) const; // "REF B song", told even while it is not measured (tooltip)
    void paint (juce::Graphics&, juce::Rectangle<int> bounds, const State&, const KirinDelta&,
                bool deltaAvailable, float pixelScale);

    // What one paint shows, without drawing: the values and where each needle rests.
    lu_face::Frame frame (const State&, const KirinDelta&, bool deltaAvailable) const;
    void advanceForTest (double seconds) { step (seconds); }
    const std::vector<lu_face::TextBox>& textForTest() const noexcept { return drawnText; }
    juce::Button& faceControl() noexcept;
    float controlFontHeightForTest() const;
    juce::Button& windowControl() noexcept;

private:
    class Control;
    void timerCallback() override;
    bool step (double seconds);
    void choose (bool luFace, lu_needle::Window);

    juce::Component& owner;
    std::unique_ptr<Control> faceButton, windowButton, referenceButton;
    lu_needle::Facts current;
    bool luSelected = false, snapNext = true, commonFace = false;
    lu_needle::Window chosenWindow = lu_needle::Window::momentary;
    lu_needle::EasedNeedle own, pre, samePlace, integrated;
    float ownTarget = 0.0f, preTarget = 0.0f, samePlaceTarget = 0.0f, integratedTarget = 0.0f;
    bool preShown = false, samePlaceShown = false;
    double lastStepMs = 0.0;
    juce::Rectangle<int> lastBounds;
    std::vector<lu_face::TextBox> drawnText;

    // The box is redrawn only when the size, role or appearance changes.
    juce::Image backdrop;
    juce::Point<int> backdropSize;
    float backdropScale = 0.0f;
    observatory::Role backdropRole = observatory::Role::pre;
    presentation::Context backdropPresentation;
    bool backdropJungle = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LuFace)
};
}
