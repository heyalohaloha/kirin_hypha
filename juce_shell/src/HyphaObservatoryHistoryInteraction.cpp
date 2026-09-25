#include "HyphaObservatoryView.h"

#include "HyphaCaptureHistoryPainter.h"
#include "HyphaChannelReadoutLayout.h"

namespace hypha::observatory
{
void View::mouseMove (const juce::MouseEvent& event)
{
    if (levelInspection.held()) return;
    if (selectedDomain == Domain::level && fullCockpit() && ! captureFrame
        && levelHistoryArea.toFloat().contains (event.position))
        levelHistoryPointer = event.position;
    else
        levelHistoryPointer.reset();
    refreshLevelHistoryHover();
}

void View::refreshLevelHistoryHover()
{
    if (levelInspection.held()) return;
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
    if (levelInspection.held()) return;
    if (! levelHistoryPointer.has_value() && ! hoveredLevelHistoryIndex.has_value())
        return;
    levelHistoryPointer.reset();
    hoveredLevelHistoryIndex.reset();
    if (! levelHistoryArea.isEmpty())
        repaint (levelHistoryArea);
}

void View::initializeLevelHistoryControls()
{
    for (auto* button : { &previousPeakButton, &nextPeakButton, &historyLiveButton, &historyCopyButton })
        addChildComponent (button);
    previousPeakButton.setComponentID ("history-previous-tp");
    nextPeakButton.setComponentID ("history-next-tp");
    historyLiveButton.setComponentID ("history-live");
    historyCopyButton.setComponentID ("history-copy");
    previousPeakButton.setTooltip ("Hold the previous TP > -1 dBTP event; measurement continues");
    nextPeakButton.setTooltip ("Hold the next TP > -1 dBTP event; measurement continues");
    historyLiveButton.setTooltip ("Resume scrolling history; measurement and session maxima are unchanged");
    historyCopyButton.setTooltip ("Copy TP window endpoint on the host clock (project or render clock, not guaranteed project time)");
    previousPeakButton.onClick = [this] { selectLevelHistoryEvent (-1); };
    nextPeakButton.onClick = [this] { selectLevelHistoryEvent (1); };
    historyLiveButton.onClick = [this] { resumeLevelHistory(); };
    historyCopyButton.onClick = [this]
    {
        if (! levelInspection.held() || target() != ObservationTarget::absolute) return;
        const auto& entry = levelInspection.snapshot[*levelInspection.index];
        auto copy = history_inspection::positionText (entry, levelInspection.sampleRate)
            + " | TP " + history_inspection::peakText (entry.true_peak.max)
            + " dBTP | host project/render clock; measured window endpoint, not exact peak or guaranteed project time";
        if (const auto* chain = levelInspection.selectedChain())
            copy += " | CONTENT END " + juce::String (chain->endpoint)
                + " | PRE M " + history_inspection::peakText (chain->pre_m)
                + " POST M " + history_inspection::peakText (chain->post_m)
                + " PRE TP " + history_inspection::peakText (chain->pre_tp)
                + " POST TP " + history_inspection::peakText (chain->post_tp)
                + " dBTP | delta M " + history_inspection::peakText (chain->delta_m)
                + " LU delta TP " + history_inspection::peakText (chain->delta_tp)
                + " dB REL " + history_inspection::peakText (chain->relation) + " dB";
        juce::SystemClipboard::copyTextToClipboard (copy);
    };
}

juce::Rectangle<int> View::levelHistoryBounds (juce::Rectangle<int> area) const
{
    const bool inspection = getWidth() >= 900;
    const bool landscape = area.getWidth() > area.getHeight();
    const auto historyHeight = juce::jlimit (72, inspection ? 240 : 170,
        juce::roundToInt (area.getHeight() * (inspection ? 0.46f : landscape ? 0.40f : 0.32f)));
    const auto previousMetricsHeight = juce::jmax (1, area.getHeight() - historyHeight - 4);
    const auto metricsHeight = juce::jmin (area.getHeight() - 92,
        juce::jmax (inspection ? 162 : 134, compressedLevelMetricsHeight (previousMetricsHeight)));
    area.removeFromTop (metricsHeight + 4);
    return area;
}

void View::layoutLevelHistoryControls()
{
    const bool visible = fullCockpit() && selectedDomain == Domain::level
        && ! captureFrame && ! hybridVuVisible() && ! recordDisplayShowing();
    auto area = bodyArea;
    if (target() == ObservationTarget::absolute)
        area.removeFromRight (channelStripWidth (presentationContext(),
            measurementOnlySurround ? (getWidth() >= 900 ? 230 : 190)
                                    : (getWidth() >= 900 ? 126 : 116)));
    auto row = levelHistoryBounds (area).reduced (2).removeFromTop (22);
    for (auto* button : { &previousPeakButton, &nextPeakButton, &historyLiveButton, &historyCopyButton })
    {
        button->setPresentationContext (presentationContext());
        button->setVisible (visible);
        button->setBounds (row.removeFromLeft (juce::jmin (72, row.getWidth())).reduced (1, 1));
    }
    updateLevelHistoryControls();
}

void View::updateLevelHistoryControls()
{
    if (! previousPeakButton.isVisible()) return;
    const auto& source = levelInspection.held() ? levelInspection.snapshot : history;
    const bool absolute = target() == ObservationTarget::absolute && cumulativeFactsAvailable()
        && ! source.empty() && source.back().measurement_epoch == observatoryFrame.meter.measurement_epoch
        && source.back().generation == observatoryFrame.meter.generation;
    previousPeakButton.setEnabled (absolute && levelInspection.event (
        history, observatoryFrame.meter.sample_rate, -1).has_value());
    nextPeakButton.setEnabled (absolute && levelInspection.event (
        history, observatoryFrame.meter.sample_rate, 1).has_value());
    historyLiveButton.setEnabled (levelInspection.held());
    historyLiveButton.setToggleState (! levelInspection.held(), juce::dontSendNotification);
    historyCopyButton.setEnabled (absolute && levelInspection.held());
}

void View::mouseDown (const juce::MouseEvent& event)
{
    if (selectedDomain != Domain::level || ! fullCockpit() || captureFrame
        || hybridVuVisible() || recordDisplayShowing() || ! event.mods.isLeftButtonDown()) return;
    const auto& entries = levelInspection.held() ? levelInspection.snapshot : history;
    const auto hit = capture_history::hitTest (levelHistoryArea, entries, event.position,
        levelInspection.held() ? levelInspection.sampleRate : observatoryFrame.meter.sample_rate);
    if (! hit) return;
    if (levelInspection.held()) levelInspection.select (*hit);
    else levelInspection.pin (history, *hit, observatoryFrame.meter,
        chainSnapshotAvailable ? &chainSnapshot : nullptr,
        chainSnapshotAvailable ? &chainPoints : nullptr);
    updateLevelHistoryControls();
    repaint (levelHistoryArea);
}

void View::selectLevelHistoryEvent (int direction)
{
    const auto at = levelInspection.event (history, observatoryFrame.meter.sample_rate, direction);
    if (! at) return;
    if (levelInspection.held()) levelInspection.select (*at);
    else levelInspection.pin (history, *at, observatoryFrame.meter,
        chainSnapshotAvailable ? &chainSnapshot : nullptr,
        chainSnapshotAvailable ? &chainPoints : nullptr);
    updateLevelHistoryControls();
    repaint (levelHistoryArea);
}

void View::resumeLevelHistory()
{
    levelInspection.clear();
    levelHistoryPointer.reset();
    hoveredLevelHistoryIndex.reset();
    updateLevelHistoryControls();
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
        if (! difference && experienceFamily() == ExperienceFamily::compactMeter
            && compactShowsMaximum && (region.metric == level_metrics::Metric::momentary
                || region.metric == level_metrics::Metric::shortTerm || region.metric == level_metrics::Metric::crest))
            help += " Showing its maximum since the last Meter Session reset.";
        return help;
    }
    if (fullCockpit() && levelHistoryArea.contains (point))
        return "Click to hold; measurement continues. HOST ~ is the window endpoint on the host project/render clock, not guaranteed project time or the exact peak. LIVE resumes scrolling.";
    return {};
}

juce::String View::getTooltip()
{
    const auto help = metricHelpAt (getMouseXYRelative());
    return help.isNotEmpty() ? help : juce::SettableTooltipClient::getTooltip();
}

}
