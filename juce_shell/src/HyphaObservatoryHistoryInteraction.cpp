#include "HyphaObservatoryView.h"

#include "HyphaCaptureHistoryPainter.h"

namespace hypha::observatory
{
void View::mouseMove (const juce::MouseEvent& event)
{
    if (selectedDomain == Domain::level && fullCockpit() && ! captureFrame
        && levelHistoryArea.toFloat().contains (event.position))
        levelHistoryPointer = event.position;
    else
        levelHistoryPointer.reset();
    refreshLevelHistoryHover();
}

void View::refreshLevelHistoryHover()
{
    const auto next = levelHistoryPointer.has_value()
        ? capture_history::hitTest (
              levelHistoryArea, history, *levelHistoryPointer,
              static_cast<double> (observatoryFrame.meter.sample_rate))
        : std::nullopt;
    if (next == hoveredLevelHistoryIndex)
        return;
    hoveredLevelHistoryIndex = next;
    if (! levelHistoryArea.isEmpty())
        repaint (levelHistoryArea);
}

void View::mouseExit (const juce::MouseEvent&)
{
    if (! levelHistoryPointer.has_value() && ! hoveredLevelHistoryIndex.has_value())
        return;
    levelHistoryPointer.reset();
    hoveredLevelHistoryIndex.reset();
    if (! levelHistoryArea.isEmpty())
        repaint (levelHistoryArea);
}

juce::Rectangle<int> View::metricHelpArea (juce::Rectangle<int> area, level_metrics::Metric metric)
{
    if (metricHelpCount < metricHelpRegions.size())
        metricHelpRegions[metricHelpCount++] = { area, metric };
    return area;
}

juce::String View::metricHelpAt (juce::Point<int> point) const
{
    if (selectedDomain != Domain::level || hybridVuVisible() || captureFrame) return {};
    for (std::size_t index = 0; index < metricHelpCount; ++index)
    {
        const auto& region = metricHelpRegions[index];
        if (! region.bounds.contains (point)) continue;
        const bool difference = target() == ObservationTarget::delta;
        auto help = juce::String (difference ? "POST minus PRE. " : role == Role::pre ? "PRE. " : "POST. ");
        help += level_metrics::scopeHelp (region.metric);
        if (! difference && experienceFamily() == ExperienceFamily::compactMeter && compactShowsMaximum
            && (region.metric == level_metrics::Metric::shortTerm || region.metric == level_metrics::Metric::crest))
            help += " Showing its maximum since the last Meter Session reset.";
        return help;
    }
    if (fullCockpit() && levelHistoryArea.contains (point))
        return "Displayed history covers 60 seconds. MAX M is the maximum momentary loudness since the last Meter Session reset.";
    return {};
}

juce::String View::getTooltip()
{
    const auto help = metricHelpAt (getMouseXYRelative());
    return help.isNotEmpty() ? help : juce::SettableTooltipClient::getTooltip();
}

}
