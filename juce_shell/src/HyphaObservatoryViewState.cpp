#include "HyphaObservatoryView.h"

namespace hypha::observatory
{
void View::setAnalysisPage (analysis_navigation::Page page)
{
    if (analysisPage == page) return;
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
    feedbackText = std::move (text);
    setTooltip (feedbackText);
    setDescription (feedbackText);
    updateControls();
    resized();
    if (hybridVuVisible()) repaint(); else repaint (sessionArea);
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

void View::toggleHybridVu()
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
}

int View::timeControlsHeight() const noexcept
{
    if (selectedDomain != Domain::time) return 0;
    const bool controls = capabilities().historyRange || capabilities().loudnessScale;
    return (role == Role::post || controls) ? timeNavigationHeight (currentPreset().density) : 0;
}
void View::setMeterContext (meter_context::MeterContext value)
{
    if (selectedMeterContext == value)
        return;
    selectedMeterContext = value;
    updateControls();
    repaint (bodyArea);
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
    return area;
}

juce::Rectangle<int> View::timeNavigationBounds() const noexcept
{
    if (selectedDomain != Domain::time)
        return {};
    const auto density = currentPreset().density;
    auto available = bodyArea;
    auto row = available.removeFromTop (timeNavigationHeight (density));
    if (capabilities().loudnessScale && ! captureFrame)
        row.removeFromRight (timeScaleWidth (density));
    if (capabilities().historyRange && ! captureFrame)
        row.removeFromRight (juce::jmax (120, timeRangeWidth (density)));
    return row;
}
}
