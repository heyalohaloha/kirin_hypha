#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaObservatoryContract.h"
#include "HyphaTheme.h"
#include "HyphaWidgets.h"
#include "kirin_hypha_ffi.h"

namespace hypha::observatory
{
class View final : public juce::Component
{
public:
    explicit View (Role roleIn);

    std::function<void (Domain)> onDomainChange;
    std::function<void (ObservationTarget)> onTargetChange;
    std::function<void (SizePreset)> onSizeChange;
    std::function<void()> onReset;
    std::function<void()> onCapture;

    void setDomain (Domain);
    Domain domain() const noexcept { return selectedDomain; }
    void setTarget (ObservationTarget);
    ObservationTarget target() const noexcept
    {
        return effectiveTarget (role, selectedDomain, selectedTarget);
    }
    void setMeterSnapshot (const KirinMeterSession&, bool available);
    void setDeltaSnapshot (const KirinDelta&, bool available);
    void setConnectionText (juce::String text, juce::Colour colour);
    void setGuide (juce::String primary, juce::String detail, bool emphasized);
    void clearGuide();
    void setHistory (std::vector<KirinMeterHistoryEntry> entries);

    struct HistoryRequest
    {
        uint8_t resolution = KIRIN_METER_HISTORY_10_HZ;
        size_t maxEntries = 300;
        const char* label = "30 S / 10 HZ";
    };

    HistoryRequest historyRequest() const noexcept;
    juce::Image createCaptureImage (int pixelWidth, int pixelHeight) const;
    juce::Rectangle<int> captureBodyBounds (int pixelWidth, int pixelHeight) const;
    juce::Rectangle<int> bodyBounds() const noexcept { return bodyArea; }
    juce::Rectangle<int> connectionBounds() const noexcept { return connectionArea; }
    juce::Rectangle<int> guideBounds() const noexcept { return guideArea; }
    juce::Rectangle<int> sessionBounds() const noexcept { return sessionArea; }
    bool bodyOwnedByExternalAnalysis() const noexcept
    {
        return selectedDomain == Domain::frequency;
    }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class TimeRange { seconds30, minutes2, minutes10, hours2, hours24 };

    void cycleDomain();
    void cycleTimeRange();
    void cycleSize();
    void updateControls();
    SizePreset currentPreset() const noexcept;
    GuidePresence guidePresence() const noexcept;
    void paintHeader (juce::Graphics&, const ShellLayout&);
    void paintGuide (juce::Graphics&, const ShellLayout&);
    void paintFooter (juce::Graphics&, const ShellLayout&);
    void paintLevel (juce::Graphics&, juce::Rectangle<int>);
    void paintChannelStrips (juce::Graphics&, juce::Rectangle<int>);
    void paintTime (juce::Graphics&, juce::Rectangle<int>);
    void paintMeasuredMycelium (juce::Graphics&, juce::Rectangle<int>);

    Role role;
    Domain selectedDomain = Domain::level;
    ObservationTarget selectedTarget = ObservationTarget::absolute;
    TimeRange timeRange = TimeRange::seconds30;
    KirinMeterSession meter {};
    KirinDelta delta {};
    bool meterAvailable = false;
    bool deltaAvailable = false;
    juce::String connectionText;
    juce::Colour connectionColour = COL_MUTED;
    juce::String guidePrimary;
    juce::String guideDetail;
    bool guideEmphasized = false;
    bool captureFrame = false;
    std::vector<KirinMeterHistoryEntry> history;
    juce::Rectangle<int> bodyArea;
    juce::Rectangle<int> connectionArea;
    juce::Rectangle<int> guideArea;
    juce::Rectangle<int> sessionArea;
    MyceliumBackground background;

    juce::TextButton levelButton { "LEVEL" };
    juce::TextButton timeButton { "TIME" };
    juce::TextButton frequencyButton { "FREQ" };
    juce::TextButton spaceButton { "SPACE" };
    juce::TextButton domainCycleButton;
    juce::TextButton targetButton;
    juce::TextButton timeRangeButton;
    juce::TextButton sizeButton;
    juce::TextButton resetButton { "RESET" };
    juce::TextButton captureButton { "CAPTURE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (View)
};
}
