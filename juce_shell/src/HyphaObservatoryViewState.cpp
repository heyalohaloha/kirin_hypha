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
    repaint (sessionArea);
}

int View::timeControlsHeight() const noexcept
{
    if (selectedDomain != Domain::time) return 0;
    const bool controls = capabilities().historyRange || capabilities().loudnessScale;
    return timeNavigationHeight (currentPreset().density)
        * ((role == Role::post ? 1 : 0) + (controls ? 1 : 0));
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
    return row;
}
}
