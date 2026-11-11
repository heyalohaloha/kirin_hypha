#include "HyphaObservatoryView.h"

namespace hypha::observatory
{
void View::configureHeaderSelectors()
{
    trackButton.setComponentID ("observatory-meter-context-track");
    twoMixButton.setComponentID ("observatory-meter-context-2mix");
    trackButton.setTitle ("Measure an individual track");
    twoMixButton.setTitle ("Measure a mix or master bus");
    for (auto* button : { &trackButton, &twoMixButton })
    {
        button->setDescription (button->getTitle());
        button->setTooltip (button->getDescription());
    }
    trackButton.onClick = [this] { chooseMeterContext (meter_context::MeterContext::trackStem); };
    twoMixButton.onClick = [this] { chooseMeterContext (meter_context::MeterContext::twoMix); };
    referenceButton.onClick = [this]
    {
        if (selectedDomain != Domain::reference && onDomainChange) onDomainChange (Domain::reference);
    };
}

void View::chooseMeterContext (meter_context::MeterContext context)
{
    if (selectedMeterContext == context && selectedDomain != Domain::reference) return;
    if (onContextChange) onContextChange (context); else setMeterContext (context);
    if (selectedDomain == Domain::reference && onDomainChange) onDomainChange (lastMeasurementDomain);
}

void View::initializeVuCalibrationControl()
{
    addChildComponent (vuCalibrationControl);
    vuCalibrationControl.onClick = [this] { if (onVuCalibrationMenu) onVuCalibrationMenu(); };
}

void View::setVuCalibration (int value, bool choiceAvailable)
{
    const auto previous = vuCalibrationControl.referenceDbfs();
    vuCalibrationControl.setReference (value);
    // The preference belongs to this track's identity, which is still being resolved.
    unavailable::set (vuCalibrationControl, choiceAvailable, "VU calibration: connecting; try again in a moment");
    if (hybridVuVisible() && previous != vuCalibrationControl.referenceDbfs()) repaint();
}

void View::setAnalysisPage (analysis_navigation::Page page)
{
    if (analysisPage == page) return;
    retireTimePresentation (true);
    analysisPage = page;
    history.clear(); runSummary = {};
    updateControls(); resized(); repaint();
}

void View::setAttackPaired (bool paired)
{
    if (attackPaired == paired) return;
    attackPaired = paired;
    updateControls(); repaint();
}

void View::setFeedback (juce::String text)
{
    if (feedbackText == text) return;
    const auto previousBody = bodyArea;
    feedbackText = std::move (text);
    setTooltip (feedbackText);
    setDescription (feedbackText);
    updateControls();
    resized();
    if (hybridVuVisible() || bodyArea != previousBody) repaint(); else repaint (sessionArea);
}

bool View::setHostRecording (bool recording)
{
    if (hostRecording == recording)
        return false;
    hostRecording = recording;
    hybridVuDismissedForCurrentRecording = false;
    updateControls();
    resized();
    repaint();
    // The view changed by itself: say why (MENU > Show Hybrid VU while recording turns it off), and
    // withdraw it when recording ends.
    if (onAutomaticNotice) onAutomaticNotice ("DAW recording: showing Hybrid VU", recordingHybridVuRequested());
    return true;
}

bool View::setHybridVuOnRecordEnabled (bool enabled)
{
    if (hybridVuOnRecordEnabled == enabled)
        return false;
    hybridVuOnRecordEnabled = enabled;
    updateControls();
    resized();
    repaint();
    return true;
}

bool View::dismissHybridVuForCurrentRecording()
{
    if (! hostRecording || ! hybridVuOnRecordEnabled
        || hybridVuDismissedForCurrentRecording)
        return false;
    hybridVuDismissedForCurrentRecording = true;
    updateControls();
    resized();
    repaint();
    return true;
}

bool View::setManualHybridVuVisible (bool visible)
{
    if (manualHybridVuSelected == visible)
        return false;
    manualHybridVuSelected = visible;
    updateControls();
    resized();
    repaint();
    return true;
}

void View::setSurroundMeasurementOnly (bool enabled)
{
    if (measurementOnlySurround == enabled)
        return;
    measurementOnlySurround = enabled;
    if (enabled)
    {
        manualHybridVuSelected = false;
        if (selectedDomain != Domain::level && selectedDomain != Domain::time)
            selectedDomain = Domain::level;
    }
    updateControls();
    resized();
    repaint();
}

void View::requestHybridVuToggle()
{
    if (hybridVuVisible())
    {
        manualHybridVuSelected = false;
        if (recordingHybridVuRequested())
            hybridVuDismissedForCurrentRecording = true;
    }
    else
        manualHybridVuSelected = true;
    updateControls();
    resized();
    repaint();
    if (onHybridVuChange) onHybridVuChange (hybridVuVisible());
}

int View::timeControlsHeight() const noexcept
{
    if (selectedDomain != Domain::time) return 0;
    const bool controls = (capabilities().historyRange || capabilities().loudnessScale)
                       && currentPreset().density != Density::compact;
    return (role == Role::post || controls) ? timeNavigationHeight (currentPreset().density) : 0;
}
void View::setMeterContext (meter_context::MeterContext value)
{
    if (selectedMeterContext == value)
        return;
    selectedMeterContext = value;
    resized();
    repaint();
}

void View::setScaleMode (meter_context::ScaleMode value)
{
    if (selectedScaleMode == value)
        return;
    selectedScaleMode = value;
    updateControls();
    repaint (bodyArea);
}

void View::setExternalAnalysisBodyActive (bool active)
{
    if (externalAnalysisBodyActive == active)
        return;
    externalAnalysisBodyActive = active;
    repaint (bodyArea);
}

void View::setRunSummaryMode (bool active)
{
    if (showRunSummary == active)
        return;
    showRunSummary = active;
    repaint (bodyArea);
}

juce::Rectangle<int> View::analysisBodyBounds() const noexcept
{
    auto area = bodyArea;
    if (selectedDomain == Domain::time)
        area.removeFromTop (timeControlsHeight());
    // Reference keeps its bottom actions clear; FREQ keeps its bottom axis labels clear.
    // Both pages end where the folded status strip begins.
    if (statusStripOverBody && (selectedDomain == Domain::reference || selectedDomain == Domain::frequency))
        area.setBottom (statusStrip.getY());
    return area;
}

juce::Rectangle<int> View::timeNavigationBounds() const noexcept
{
    if (selectedDomain != Domain::time)
        return {};
    const auto density = currentPreset().density;
    auto available = bodyArea;
    auto row = available.removeFromTop (timeNavigationHeight (density));
    if (capabilities().loudnessScale && timeControlsShown())
        row.removeFromRight (timeScaleWidth (density));
    if (capabilities().historyRange && timeControlsShown())
        row.removeFromRight (juce::jmax (120, timeRangeWidth (density)));
    return row;
}
}
