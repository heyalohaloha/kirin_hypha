#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaCaptureContract.h"
#include "HyphaMeterContext.h"
#include "HyphaObservatoryContract.h"
#include "HyphaObservatoryPresentation.h"
#include "HyphaObservatoryWorld.h"
#include "HyphaTheme.h"
#include "HyphaWidgets.h"
#include "kirin_hypha_ffi.h"

namespace hypha::observatory
{
class Button final : public juce::TextButton
{
public:
    Button (juce::String text, bool tabIn);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool tab = false;
};

class View final : public juce::Component
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
    void setTarget (ObservationTarget);
    ObservationTarget target() const noexcept
    {
        return effectiveTarget (role, selectedDomain, selectedTarget);
    }
    ObservationTarget preferredTarget() const noexcept { return selectedTarget; }
    void setTimeRange (TimeRange);
    TimeRange selectedTimeRange() const noexcept { return timeRange; }
    void setMeterSnapshot (const KirinMeterSession&, bool available);
    void setDeltaSnapshot (const KirinDelta&, bool available);
    void setObservatoryFrame (const KirinObservatoryFrame&, bool available);
    void setWatchDisplay (const KirinWatchDisplay&, bool available);
    void setShortTermLoudness (bool);
    bool shortTermLoudness() const noexcept { return selectedShortTermLoudness; }
    void setCompactMaximum (bool);
    bool compactMaximum() const noexcept { return compactShowsMaximum; }
    void setMeterContext (meter_context::MeterContext);
    meter_context::MeterContext meterContext() const noexcept { return selectedMeterContext; }
    void setScaleMode (meter_context::ScaleMode);
    meter_context::ScaleMode scaleMode() const noexcept { return selectedScaleMode; }
    void setExternalAnalysisBodyActive (bool);
    void setConnection (juce::String text, juce::Colour colour, ConnectionState state);
    void setExternalConnectionLabelVisible (bool visible);
    ConnectionState connection() const noexcept { return connectionState; }
    void setGuide (juce::String primary, juce::String detail, bool emphasized);
    void clearGuide();
    void setHistory (std::vector<KirinMeterHistoryEntry> entries);
    // The editor may render a physical preset through a scaled logical viewport. This affects
    // only the size label/cycle identity; measurement and shell layout keep using local bounds.
    void setDisplayedEditorSize (int width, int height);

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
    void updateControls();
    SizePreset currentPreset() const noexcept;
    GuidePresence guidePresence() const noexcept;
    void paintHeader (juce::Graphics&, const ShellLayout&);
    void paintGuide (juce::Graphics&, const ShellLayout&);
    void paintFooter (juce::Graphics&, const ShellLayout&);
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
    bool compactShowsMaximum = false;
    meter_context::MeterContext selectedMeterContext = meter_context::defaultContext;
    meter_context::ScaleMode selectedScaleMode = meter_context::defaultScale;
    bool externalAnalysisBodyActive = false;
    juce::String connectionText;
    juce::Colour connectionColour = COL_MUTED;
    ConnectionState connectionState = ConnectionState::unpaired;
    bool externalConnectionLabelVisible = false;
    juce::String guidePrimary;
    juce::String guideDetail;
    bool guideEmphasized = false;
    bool captureFrame = false;
    juce::String captureTimestamp;
    juce::String captureVersion;
    capture::DisplayMetadata captureMetadata;
    std::vector<KirinMeterHistoryEntry> history;
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
    Button resetButton { "RESET", false };
    Button captureButton { "CAPTURE", false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (View)
};
}
