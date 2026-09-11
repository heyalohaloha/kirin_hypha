#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaCaptureContract.h"
#include "HyphaMeterContext.h"
#include "HyphaInformationButton.h"
#include "HyphaObservatoryContract.h"
#include "HyphaObservationPageContract.h"
#include "HyphaObservatoryPresentation.h"
#include "HyphaObservatoryWorld.h"
#include "HyphaRunSummary.h"
#include "HyphaTheme.h"
#include "HyphaWidgets.h"
#include "kirin_hypha_ffi.h"

namespace hypha::observatory
{
class Button final : public juce::TextButton
{
public:
    Button (juce::String text, bool tabIn);
    void setPresentationContext (presentation::Context next) noexcept
    {
        presentationContext = next;
        repaint();
    }
    float fontHeightForTest() const
    {
        return labelFont (presentationContext, typography::TextRole::action).getHeight();
    }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool tab = false;
    presentation::Context presentationContext = presentation::defaultContext();
};

class View final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit View (Role roleIn);

    std::function<void (Domain)> onDomainChange;
    std::function<void (ObservationTarget)> onTargetChange;
    std::function<void (TimeRange)> onTimeRangeChange;
    std::function<void (SizePreset)> onSizeChange;
    std::function<void (bool)> onLoudnessChange;
    std::function<void (meter_context::MeterContext)> onContextChange;
    std::function<void (meter_context::ScaleMode)> onScaleChange;
    std::function<void()> onReset;
    std::function<void()> onCapture;
    std::function<void()> onNote;
    std::function<void()> onInformation;
    std::function<void()> onLocalBlind;
    std::function<void()> onDomainMenu;
    std::function<void()> onSizeMenu;
    std::function<void()> onOperationsMenu;
    std::function<void()> onStop;
    std::function<void()> onGuideDetails;
    std::function<void()> onFeedbackDetails;
    std::function<void (bool)> onHybridVuChange;
    std::function<void()> onClearPeakClipHolds;
    juce::Component& informationAnchor() noexcept { return informationButton; }
    juce::Component& domainMenuAnchor() noexcept { return domainCycleButton; }
    juce::Component& sizeMenuAnchor() noexcept { return sizeButton; }
    juce::Component& operationsMenuAnchor() noexcept { return operationsButton; }
    juce::Component& guideDetailsAnchor() noexcept { return guideButton; }
    juce::Component& feedbackDetailsAnchor() noexcept { return statusButton; }

    void setDomain (Domain);
    Domain domain() const noexcept { return selectedDomain; }
    ExperienceFamily experienceFamily() const noexcept
    {
        return observatory::experienceFamily (currentPreset());
    }
    bool fullCockpit() const noexcept
    {
        return isFullDensity (currentPreset().density);
    }
    PresentationContract presentation() const noexcept
    {
        return presentationContract (currentPreset());
    }
    presentation::Context presentationContext() const noexcept
    {
        return presentation::forOutput (getWidth(), getHeight(), presentationOutput);
    }
    void setTarget (ObservationTarget);
    void setDeltaTargetEnabled (bool enabled);
    bool deltaTargetEnabledForTest() const noexcept { return deltaTargetEnabled; }
    bool deltaTargetControlEnabledForTest() const noexcept
    { return fullCockpit() ? deltaButton.isEnabled() : targetButton.isEnabled(); }
    ObservationTarget target() const noexcept
    {
        return capabilities().target;
    }
    ObservationTarget preferredTarget() const noexcept { return selectedTarget; }
    PageCapabilities capabilities() const noexcept
    { return pageCapabilities (role, selectedDomain, analysisPage, selectedTarget, attackPaired); }
    void setAnalysisPage (analysis_navigation::Page);
    void setAttackPaired (bool);
    void setFeedback (juce::String text);
    int timeControlsHeight() const noexcept;
    void setTimeRange (TimeRange);
    TimeRange selectedTimeRange() const noexcept { return timeRange; }
    void setMeterSnapshot (const KirinMeterSession&, bool available);
    void setDeltaSnapshot (const KirinDelta&, bool available);
    void setObservatoryFrame (const KirinObservatoryFrame&, bool available);
    void setWatchDisplay (const KirinWatchDisplay&, bool available);
    void setShortTermLoudness (bool);
    bool shortTermLoudness() const noexcept { return selectedShortTermLoudness; }
    bool setHostRecording (bool recording);
    bool setHybridVuOnRecordEnabled (bool enabled);
    bool dismissHybridVuForCurrentRecording();
    bool setManualHybridVuVisible (bool visible);
    bool manualHybridVuVisible() const noexcept { return manualHybridVuSelected; }
    bool hybridVuShownByRecording() const noexcept
    {
        return ! manualHybridVuSelected && recordingHybridVuRequested() && ! captureFrame;
    }
    bool hybridVuVisible() const noexcept
    {
        return (manualHybridVuSelected || recordingHybridVuRequested()) && ! captureFrame;
    }
    void setCompactMaximum (bool);
    bool compactMaximum() const noexcept { return compactShowsMaximum; }
    void setMeterContext (meter_context::MeterContext);
    meter_context::MeterContext meterContext() const noexcept { return selectedMeterContext; }
    void setScaleMode (meter_context::ScaleMode);
    meter_context::ScaleMode scaleMode() const noexcept { return selectedScaleMode; }
    void setExternalAnalysisBodyActive (bool);
    void setRunSummaryMode (bool);
    bool runSummaryAvailable() const noexcept { return runSummary.available(); }
    void setReferenceOwned (bool owned)
    {
        referenceOwned = owned;
        referenceButton.setEnabled (role == Role::post);
        referenceButton.setTitle (owned ? "Reference" : "Reference - About Kirin OS");
        referenceButton.setDescription (owned ? "Open Reference audition"
                                             : "Open Kirin OS information and connection help");
        referenceButton.setTooltip (referenceButton.getDescription());
        repaint();
    }
    bool isReferenceOwned() const noexcept { return referenceOwned; }
    void setConnection (juce::String text, juce::Colour colour, ConnectionState state);
    void setExternalConnectionLabelVisible (bool visible);
    void setJungleAppearance (bool enabled)
    {
        if (jungleAppearance == enabled) return;
        jungleAppearance = enabled;
        repaint();
    }
    bool jungleAppearanceEnabled() const noexcept { return jungleAppearance; }
    bool jungleAppearanceEnabledForTest() const noexcept { return jungleAppearanceEnabled(); }
    ConnectionState connection() const noexcept { return connectionState; }
    void setGuide (juce::String primary, juce::String detail, bool emphasized);
    void clearGuide();
    void setHistory (std::vector<KirinMeterHistoryEntry> entries);
    // The editor may render a physical preset through a scaled logical viewport. This affects
    // only the size label/cycle identity; measurement and shell layout keep using local bounds.
    void setDisplayedEditorSize (int width, int height);
    void setNoteAvailability (bool osOwned, bool recording);
    void setKeepActive (bool active);
    bool localBlindEntryAvailable() const noexcept { return localBlindEntryEnabled; }
    const juce::String& feedback() const noexcept { return feedbackText; }
    void setLocalBlindEntryEnabled (bool enabled)
    {
        if (localBlindEntryEnabled == enabled) return;
        localBlindEntryEnabled = enabled;
        resized();
    }

    struct HistoryRequest
    {
        uint8_t resolution = KIRIN_METER_HISTORY_10_HZ;
        size_t maxEntries = 300;
        size_t maxOutputEntries = 300;
        const char* label = "30 S / 10 HZ";
    };

    HistoryRequest historyRequest() const noexcept;
    juce::Image createCaptureImage (int pixelWidth, int pixelHeight,
                                    bool includeGuide = false,
                                    juce::String capturedAt = {},
                                    juce::String productVersion = {},
                                    capture::DisplayMetadata metadata = {},
                                    const std::vector<KirinMeterHistoryEntry>* historySnapshot = nullptr) const;
    juce::Rectangle<int> captureBodyBounds (int pixelWidth, int pixelHeight,
                                            bool includeGuide = false) const;
    juce::Rectangle<int> bodyBounds() const noexcept { return bodyArea; }
    juce::Rectangle<int> analysisBodyBounds() const noexcept;
    juce::Rectangle<int> timeNavigationBounds() const noexcept;
    juce::Rectangle<int> connectionBounds() const noexcept { return connectionArea; }
    juce::Rectangle<int> guideBounds() const noexcept { return guideArea; }
    juce::Rectangle<int> sessionBounds() const noexcept { return sessionArea; }
    std::uint64_t captureHistoryEndpoint() const noexcept
    {
        return frameAvailable ? observatoryFrame.meter.observed_frames : 0u;
    }
    bool bodyOwnedByExternalAnalysis() const noexcept
    {
        return role == Role::post
            && (selectedDomain == Domain::frequency || selectedDomain == Domain::reference);
    }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void cycleDomain();
    void cycleTimeRange();
    void cycleSize();
    void toggleHybridVu();
    bool recordingHybridVuRequested() const noexcept
    {
        return hostRecording && hybridVuOnRecordEnabled
            && ! hybridVuDismissedForCurrentRecording;
    }
    void updateControls();
    SizePreset currentPreset() const noexcept;
    GuidePresence guidePresence() const noexcept;
    void paintHeader (juce::Graphics&, const ShellLayout&);
    void paintFooter (juce::Graphics&, const ShellLayout&);
    void layoutFooterActions (juce::Rectangle<int>);
    void paintLevel (juce::Graphics&, juce::Rectangle<int>, bool includeChannelStrips = true);
    void paintLevelWithHistory (juce::Graphics&, juce::Rectangle<int>);
    void paintChannelStrips (juce::Graphics&, juce::Rectangle<int>);
    void paintTime (juce::Graphics&, juce::Rectangle<int>);
    void refreshLevelHistoryHover();
    observatory_world::State worldState() const noexcept;
    bool currentFactsAvailable() const noexcept;
    bool cumulativeFactsAvailable() const noexcept;
    bool deltaFactsAvailable() const noexcept;

    Role role;
    Domain selectedDomain = Domain::level;
    ObservationTarget selectedTarget = ObservationTarget::absolute;
    TimeRange timeRange = TimeRange::seconds30;
    KirinObservatoryFrame observatoryFrame {};
    bool frameAvailable = false;
    KirinWatchDisplay watchDisplay {};
    bool watchDisplayAvailable = false;
    bool selectedShortTermLoudness = false;
    bool hostRecording = false;
    bool hybridVuOnRecordEnabled = true;
    bool hybridVuDismissedForCurrentRecording = false;
    bool manualHybridVuSelected = false;
    bool compactShowsMaximum = false;
    meter_context::MeterContext selectedMeterContext = meter_context::defaultContext;
    meter_context::ScaleMode selectedScaleMode = meter_context::defaultScale;
    bool externalAnalysisBodyActive = false;
    bool showRunSummary = false;
    analysis_navigation::Page analysisPage = analysis_navigation::Page::meters;
    bool attackPaired = false;
    bool deltaTargetEnabled = true;
    juce::String feedbackText;
    bool referenceOwned = false;
    bool localBlindEntryEnabled = false;
    bool keepActive = false;
    juce::String connectionText;
    juce::Colour connectionColour = COL_MUTED;
    ConnectionState connectionState = ConnectionState::unpaired;
    bool externalConnectionLabelVisible = false;
    juce::String guidePrimary;
    juce::String guideDetail;
    bool guideEmphasized = false;
    bool captureFrame = false;
    bool jungleAppearance = false;
    presentation::OutputTarget presentationOutput = presentation::OutputTarget::editor;
    juce::String captureTimestamp;
    juce::String captureVersion;
    capture::DisplayMetadata captureMetadata;
    std::vector<KirinMeterHistoryEntry> history;
    run_summary::Result runSummary;
    juce::Rectangle<int> bodyArea;
    juce::Rectangle<int> connectionArea;
    juce::Rectangle<int> guideArea;
    juce::Rectangle<int> sessionArea;
    juce::Rectangle<int> levelHistoryArea;
    std::optional<juce::Point<float>> levelHistoryPointer;
    std::optional<std::size_t> hoveredLevelHistoryIndex;
    observatory_world::Backdrop background;
    int displayedEditorWidth = 0;
    juce::String displayedSizeLabel;

    Button levelButton { "LEVEL", true };
    Button timeButton { "TIME", true };
    Button frequencyButton { "FREQ", true };
    Button spaceButton { "SPACE", true };
    Button referenceButton { "REF", true };
    Button domainCycleButton { {}, true };
    Button targetButton { {}, false };
    Button deltaButton { hypha::delta(), false };
    Button timeRangeButton { {}, false };
    Button compactLoudnessButton { {}, false };
    Button compactRangeButton { {}, false };
    Button contextButton { {}, false };
    Button scaleButton { {}, false };
    Button sizeButton { {}, false };
    Button operationsButton { "MENU", false };
    Button stopButton { "STOP", false };
    Button guideButton { {}, false };
    Button statusButton { {}, true };
    Button hybridVuButton { "VU", false };
    Button clearPeakClipButton { "CLEAR", false };
    Button resetButton { "RESET", false };
    Button noteButton { "NOTE", false };
    Button captureButton { "CAPTURE", false };
    Button localBlindButton { "BLIND", false };
    InformationButton informationButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (View)
};
}
