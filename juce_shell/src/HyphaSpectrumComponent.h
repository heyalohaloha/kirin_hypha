#pragma once

#include <array>
#include <cstring>
#include <functional>
#include "HyphaPairingLoopText.h"
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTheme.h"
#include "HyphaAbsoluteSpectrumHistory.h"
#include "HyphaGuideFrequencyOverlay.h"
#include "HyphaSpectrumFocusTrail.h"
#include "HyphaSpectrumDeltaSelection.h"
#include "HyphaSpectrumPresentation.h"
#include "HyphaSpectrumAverage.h"
#include "HyphaSpectrumDisplay.h"
#include "HyphaSpectrumFrameDriver.h"
#include "kirin_hypha_ffi.h"
#include "kirin_hypha_display_ffi.h"

namespace hypha
{
// POST-only presentation component. It receives fixed Rust snapshots and owns no source timer,
// file, FFT, pairing, or audio state. VBlank advances only the drawing. Spatial calm and curve ballistics remain display-only; raw analysis
// stays in Rust and an incompatible exact frame becomes a factual status.
class SpectrumComponent final : public juce::Component,
                                public juce::SettableTooltipClient
{
public:
    SpectrumComponent();
    using Average = spectrum_average::Choice;
    Average averageChoice() const noexcept { return average; }
    void setAverageChoice (Average next)
    {
        if (! spectrum_average::valid (static_cast<int> (next))) next = Average::live;
        if (average == next) return;
        average = next;
        averageButton.setButtonText (juce::String ("AVG ") + spectrum_average::label (average));
        curveDirty = havePendingSnapshot;
        repaint();
    }
    std::function<void (Average)> onAverageChange;
    void chooseAverage (int item)
    {
        if (! spectrum_average::valid (item - 1)) return;
        const auto next = static_cast<Average> (item - 1);
        setAverageChoice (next);
        if (onAverageChange) onAverageChange (next);
    }
    juce::PopupMenu averageMenu() const
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("AVG");
        for (auto option : spectrum_average::choices)
            menu.addItem (static_cast<int> (option) + 1, spectrum_average::label (option), true, option == average);
        return menu;
    }

    void setPresentationContext (presentation::Context next)
    {
        if (presentationContext == next) return;
        presentationContext = next;
        repaint();
    }

    void setDisplaySettings (display_settings::Values next)
    {
        if (displaySettings == next) return;
        displaySettings = next; curveDirty = havePendingSnapshot; backdrop.invalidate(); repaint();
    }
    display_settings::Values displaySettingsForTest() const noexcept { return displaySettings; }
    double displayedPsbCeilingForTest() const noexcept { return psbMotion.ceiling; }
    double peakLabelUpdateTimeForTest() const noexcept { return peakReadout.updated; }
    void setHistoryCacheEnabledForTest (bool enabled) { backdrop.enabled = enabled; backdrop.invalidate(); }
    unsigned backdropBuildsForTest() const noexcept { return backdrop.builds; }
    void setAnimationVisible (bool shown) { animationVisible = shown; frames.setEnabled (shown && signalActive); }
    bool frameDriverActiveForTest() const noexcept { return frames.active(); }
    double displayedPsbForTest (size_t band) const noexcept { return psbMotion.shown[band]; }
    const spectrum_peaks::Peaks& peakLabelsForTest() const noexcept { return peakReadout.peaks; }
    void setSnapshot (const KirinSpectrumView& next);
    void setMidSideSnapshot (const KirinMidSideSpectrumView& next, uint64_t continuity = 0);
    void setBatch (const KirinSpectrumBatch& batch, uint64_t postContinuity = 0, uint64_t deltaContinuity = 0);
    void queueSnapshot (const KirinSpectrumView& next, uint64_t continuity = 0);
    void clearSnapshot();
    void presentationTick();
    void presentationTickAt (double nowMs);
    void setAnalysisOwnerNames (const juce::String& names);
    void setComparisonStatus (const juce::String& status)
    {
        if (comparisonStatus == status) return;
        comparisonStatus = status;
        repaint();
    }
    // FREQ and PSB share the callback reason with SHARP and the listening controls.
    void setPairingLoopBlock (live_compare::LoopPairingBlock blocked)
    {
        if (pairingLoopBlock == blocked) return;
        pairingLoopBlock = blocked;
        repaint();
    }
    // The PSB window's standing text from what the editor last told this view; one rule for the
    // paint and for tests.
    juce::String psbStatusText() const;
    void setGuideFrequencyOverlay (const guide_frequency::Overlay& next);
    void setAbsoluteObservation (bool absolute);
    void setDisplaySelection (uint8_t selection);
    void setLowScope (bool low);
    bool isLowScope() const noexcept { return lowScope; }
    void setPsbSnapshot (const KirinPsbView&);
    bool isPsbObservation() const noexcept { return psbObservation; }
    bool isShapeObservationForTest() const noexcept { return shapeObservation; }
    bool isMidSideObservation() const noexcept { return midSideObservation; }
    std::function<void()> onSubviewChange;
    void setSignalActive (bool active);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool hasFocusLock() const noexcept { return focusFrequencyHz > 0.0f; }
    float focusLockFrequencyHz() const noexcept { return focusFrequencyHz; }
    bool hasMark() const noexcept { return haveMark; }
    uint8_t channelModeForTest() const noexcept { return channelMode; }
    size_t focusTrailSizeForTest() const noexcept
    {
        return focusTrail != nullptr ? focusTrail->size() : 0u;
    }
    float focusTrailValueForTest (size_t frame, float band) const noexcept
    { return focusTrail != nullptr ? focusTrail->valueAt (frame, band) : NAN; }
    float markValueForTest (size_t index) const noexcept { return markedDelta[index]; }
    int64_t presentedEndpointForTest() const noexcept
    {
        return snapshot.presentation_end_samples;
    }
    float readoutDeltaForTest (size_t index) const noexcept
    {
        return index < readoutDelta.size() ? readoutDelta[index] : 0.0f;
    }
    bool readoutDeltaValidForTest (size_t index) const noexcept
    {
        return index < readoutDeltaValid.size() && readoutDeltaValid[index] != 0u;
    }
    float readoutMidForTest (size_t index) const noexcept
    { return index < readoutPre.size() ? readoutPre[index] : 0.0f; }
    float readoutSideForTest (size_t index) const noexcept
    { return index < readoutPost.size() ? readoutPost[index] : 0.0f; }
    float displayedPostForTest (size_t index) const noexcept
    { return index < displayedPost.size() ? displayedPost[index] : 0.0f; }
    float displayedPreForTest (size_t index) const noexcept
    { return index < displayedPre.size() ? displayedPre[index] : 0.0f; }
    float displayedDeltaForTest (size_t index) const noexcept
    { return index < displayedDelta.size() ? displayedDelta[index] : 0.0f; }
    bool displayedDeltaValidForTest (size_t index) const noexcept
    { return index < displayedDeltaValid.size() && displayedDeltaValid[index] != 0u; }
    double curveUpdateTimeForTest() const noexcept { return lastCurveUpdateMs; }
    float pendingPostForTest (size_t index) const noexcept
    { return index < pendingPost.size() ? pendingPost[index] : 0.0f; }
    bool isAbsoluteObservationForTest() const noexcept { return absoluteObservation; }
    bool isPsbObservationForTest() const noexcept { return psbObservation; }
    size_t absoluteHistorySizeForTest() const noexcept { return absoluteHistory.size(); }
    float absolutePeakHoldForTest (size_t index) const noexcept
    {
        return index < KIRIN_SPECTRUM_BAND_COUNT
            ? absoluteHistory.peakHold()[index] : 0.0f;
    }

    std::function<bool(uint8_t)> onChannelModeChange;
    std::function<bool(bool)> onScopeChange;

private:
    display_settings::Values displaySettings;
    spectrum_display::PeakReadout peakReadout;
    spectrum_display::PsbMotion psbMotion;
    spectrum_display::Backdrop backdrop;
    bool resumeCurve = false, animationVisible = true;
    class AverageButton final : public juce::Button
    {
    public:
        explicit AverageButton (SpectrumComponent&);
        void paintButton (juce::Graphics&, bool, bool) override;
        bool keyPressed (const juce::KeyPress&) override;
    private:
        SpectrumComponent& owner;
    };
    AverageButton averageButton { *this };
    void showAverageMenu();
    void clearInteractionState() noexcept;
    bool currentSnapshotValid() const noexcept;
    bool currentPostSnapshotValid() const noexcept;
    KirinSpectrumView snapshot {};
    KirinMidSideSpectrumView midSideSnapshot {};
    uint64_t midSideContinuity = 0;
    KirinSpectrumView pendingSnapshot {};
    KirinSpectrumView interactionDefinition {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> displayedPre {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> displayedPost {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> displayedDelta {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> readoutPre {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> readoutPost {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> readoutDelta {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> pendingPre {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> pendingPost {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> pendingDelta {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> markedDelta {};
    spectrum_delta::Validity displayedDeltaValid {};
    spectrum_delta::Validity averagedDeltaValid {};
    spectrum_delta::Validity readoutDeltaValid {};
    spectrum_delta::Validity pendingDeltaValid {};
    spectrum_delta::Validity markedDeltaValid {};
    std::unique_ptr<spectrum_focus::FocusTrailHistory> focusTrail;
    bool haveSnapshot = false;
    bool havePendingSnapshot = false;
    uint64_t spectrumContinuity = 0;
    bool haveInteractionDefinition = false;
    bool curveDirty = false;
    bool numericDirty = false;
    bool haveMark = false;
    float hoverNormalisedX = -1.0f;
    float focusFrequencyHz = -1.0f;
    uint8_t channelMode = KIRIN_SPECTRUM_CHANNEL_LR;
    uint8_t inputChannels = 0;
    juce::String modeActionNotice;
    juce::String analysisOwnerNames;
    juce::String comparisonStatus;
    guide_frequency::Overlay guideOverlay;
    absolute_spectrum::History absoluteHistory;
    bool absoluteObservation = false;
    bool midSideObservation = false;
    bool lowScope = false;
    bool midSideSnapshotValid = false;
    std::array<double, 20> absolutePsb {};
    std::array<double, 20> deltaPsb {};
    bool absolutePsbAvailable = false;
    bool deltaPsbAvailable = false;
    bool psbObservation = false;
    bool shapeObservation = false;
    Average average = Average::live;
    bool signalActive = false;
    live_compare::LoopPairingBlock pairingLoopBlock = live_compare::LoopPairingBlock::none;
    uint8_t psbStatus = KIRIN_SPECTRUM_WARMING_UP;
    int psbHoverBand = -1;
    double modeActionNoticeUntilMs = 0.0;
    bool hoverNeedsRepaint = false;
    presentation::Context presentationContext = presentation::defaultContext();
    double lastCurvePresentationMs = 0.0;
    double lastCurveUpdateMs = 0.0;
    double lastNumericPresentationMs = 0.0;

    spectrum_display::FrameDriver frames { *this, [this] (double now) { presentationTickAt (now); } };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumComponent)
};
}
