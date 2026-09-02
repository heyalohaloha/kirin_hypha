#include "HyphaSpectrumComponent.h"

#include "HyphaAnalysisUiText.h"
#include "HyphaSpectrumGeometry.h"

namespace hypha
{
namespace
{
    juce::Rectangle<float> legendBounds (
        juce::Rectangle<float> outer, float scale, int x, int width)
    {
        return { outer.getX()
                    + (float) (ui_contract::spectrumLegendAfterChannelModes + x) * scale,
                 outer.getY() + (float) ui_contract::spectrumLegendTop * scale,
                 (float) width * scale,
                 (float) ui_contract::spectrumLegendHeight * scale };
    }
}

void SpectrumComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto bounds = getLocalBounds().toFloat();
    const float scale = spectrum_geometry::visualScaleFor (bounds);
    const auto outer = spectrum_geometry::plotBoundsFor (bounds);
    const auto plot = spectrum_geometry::dataPlotBoundsFor (bounds);
    const auto position = event.position;
    juce::String tip;

    for (size_t index = 0; index < ui_contract::spectrumChannelModeWidths.size(); ++index)
        if (spectrum_geometry::channelModeBoundsFor (index, outer, scale).contains (position))
            tip = analysis_ui::channelModeTooltip (static_cast<uint8_t> (index));

    const auto mark = spectrum_geometry::markBoundsFor (outer, scale);
    if (! absoluteObservation && tip.isEmpty()
        && focusFrequencyHz <= 0.0f && mark.contains (position))
        tip = analysis_ui::markTooltip (
            haveMark && spectrum_geometry::markClearBoundsFor (mark, scale).contains (position));

    const bool expanded = scale > 1.1f;
    if (tip.isEmpty() && focusFrequencyHz > 0.0f)
    {
        const auto readout = spectrum_geometry::readoutBoundsFor (outer, scale, expanded, true);
        if (readout.contains (position))
            tip = spectrum_geometry::focusClearBoundsFor (readout, scale).contains (position)
                    ? analysis_ui::focusTrailTooltip (true)
                    : focusFrequencyHz < snapshot.approximate_below_hz
                        ? analysis_ui::approximateFrequencyTooltip()
                        : analysis_ui::focusTrailTooltip (false);
        else if (! absoluteObservation
                 && spectrum_geometry::focusTrailBoundsFor (bounds).contains (position))
            tip = analysis_ui::focusTrailTooltip (false);
    }

    if (tip.isEmpty() && focusFrequencyHz <= 0.0f)
    {
        if (! absoluteObservation
            && legendBounds (outer, scale, ui_contract::spectrumDeltaLegendLabelX,
                          ui_contract::spectrumDeltaLegendLabelWidth).contains (position))
            tip = analysis_ui::deltaLegendTooltip();
        else if (! absoluteObservation
                 && legendBounds (outer, scale, ui_contract::spectrumPreLegendLabelX,
                               ui_contract::spectrumPreLegendLabelWidth).contains (position))
            tip = analysis_ui::preLegendTooltip();
        else if (legendBounds (outer, scale, ui_contract::spectrumPostLegendLabelX,
                               ui_contract::spectrumPostLegendLabelWidth).contains (position))
            tip = analysis_ui::postLegendTooltip();
    }

    const float controlsBottom = outer.getY()
        + (float) (ui_contract::spectrumChannelModeTop
                 + ui_contract::spectrumChannelModeHeight) * scale;
    const float next = plot.contains (position) && position.y >= controlsBottom
                         ? juce::jlimit (0.0f, 1.0f,
                                        (position.x - plot.getX()) / plot.getWidth())
                         : -1.0f;
    if (tip.isEmpty() && next >= 0.0f)
    {
        const float frequency = snapshot.min_hz > 0.0f && snapshot.max_hz > snapshot.min_hz
            ? spectrum_geometry::frequencyForProbeNormalisedX (
                next, snapshot.min_hz, snapshot.max_hz)
            : 0.0f;
        tip = frequency > 0.0f && frequency < snapshot.approximate_below_hz
                ? analysis_ui::approximateFrequencyTooltip()
                : absoluteObservation ? analysis_ui::absoluteSpectrumPlotTooltip()
                                      : analysis_ui::spectrumPlotTooltip();
    }
    if (tip != getTooltip())
        setTooltip (tip);
    if (! juce::approximatelyEqual (next, hoverNormalisedX))
    {
        hoverNormalisedX = next;
        hoverNeedsRepaint = true;
    }
}

void SpectrumComponent::mouseExit (const juce::MouseEvent&)
{
    if (getTooltip().isNotEmpty())
        setTooltip ({});
    if (hoverNormalisedX >= 0.0f)
    {
        hoverNormalisedX = -1.0f;
        hoverNeedsRepaint = true;
    }
}
}
