#include "HyphaObservatoryView.h"

namespace hypha::observatory
{
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
        area.removeFromTop (timeNavigationHeight (currentPreset().density));
    return area;
}

juce::Rectangle<int> View::timeNavigationBounds() const noexcept
{
    if (selectedDomain != Domain::time)
        return {};
    const auto density = currentPreset().density;
    auto available = bodyArea;
    auto row = available.removeFromTop (timeNavigationHeight (density));
    row.removeFromRight (timeScaleWidth (density) + timeRangeWidth (density));
    return row;
}
}
