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
}
