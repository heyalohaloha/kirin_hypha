#include "HyphaSpectrumComponent.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumUiContract.h"

#include <algorithm>

namespace hypha
{
void SpectrumComponent::mouseDown (const juce::MouseEvent& event)
{
    const auto bounds = getLocalBounds().toFloat();
    const float scale = spectrum_geometry::visualScaleFor (bounds);
    const auto outerPlot = spectrum_geometry::plotBoundsFor (bounds);
    if (spectrum_geometry::subviewBoundsFor (outerPlot, scale).contains (event.position))
    {
        psbObservation = ! psbObservation;
        clearSnapshot();
        absolutePsbAvailable = deltaPsbAvailable = false;
        psbStatus = KIRIN_SPECTRUM_WARMING_UP;
        if (onSubviewChange) onSubviewChange();
        psbHoverBand = -1;
        hoverNormalisedX = -1.0f;
        setTooltip (psbObservation ? "PSB: perceptual share by Bark band"
                                   : "Spectrum: frequency level and difference");
        repaint();
        return;
    }
    if (psbObservation) return;
    const auto plot = spectrum_geometry::dataPlotBoundsFor (
        bounds, ! absoluteObservation && focusFrequencyHz > 0.0f);
    for (size_t index = 0; index < ui_contract::spectrumDisplayModeWidths.size(); ++index)
    {
        if (! spectrum_geometry::displayModeBoundsFor (
                index, outerPlot, scale).contains (event.position))
            continue;
        const auto requestedMode = static_cast<uint8_t> (index);
        if (requestedMode == channelMode)
            return;
        const bool stereoOnly = requestedMode == KIRIN_SPECTRUM_CHANNEL_SIDE
                             || requestedMode == KIRIN_SPECTRUM_SELECTION_MID_SIDE;
        const bool unavailable = (stereoOnly && inputChannels != 2u)
                              || (requestedMode == KIRIN_SPECTRUM_SELECTION_MID_SIDE
                                  && ! absoluteObservation);
        const bool accepted = ! unavailable && onChannelModeChange
                           && onChannelModeChange (requestedMode);
        if (! accepted)
        {
            modeActionNotice = unavailable
                ? requestedMode == KIRIN_SPECTRUM_SELECTION_MID_SIDE
                    ? "M/S -- POST STEREO" : "SIDE -- STEREO"
                : "MODE --";
            modeActionNoticeUntilMs = juce::Time::getMillisecondCounterHiRes() + 1'500.0;
            repaint();
            return;
        }
        snapshot = {};
        pendingSnapshot = {};
        displayedPre.fill (0.0f);
        displayedPost.fill (0.0f);
        displayedDelta.fill (0.0f);
        readoutPre.fill (0.0f);
        readoutPost.fill (0.0f);
        readoutDelta.fill (0.0f);
        pendingPre.fill (0.0f);
        pendingPost.fill (0.0f);
        pendingDelta.fill (0.0f);
        displayedDeltaValid.fill (0u);
        readoutDeltaValid.fill (0u);
        pendingDeltaValid.fill (0u);
        clearInteractionState();
        haveSnapshot = false;
        havePendingSnapshot = false;
        curveDirty = false;
        numericDirty = false;
        channelMode = requestedMode;
        midSideObservation = requestedMode == KIRIN_SPECTRUM_SELECTION_MID_SIDE;
        repaint();
        return;
    }

    if (! absoluteObservation && ! midSideObservation)
    {
        const auto selector = spectrum_geometry::deltaModeBoundsFor (outerPlot, scale);
        if (selector.contains (event.position))
        {
            const bool requestedShape = event.position.x >= selector.getCentreX();
            if (requestedShape != shapeObservation)
            {
                const auto latest = havePendingSnapshot ? pendingSnapshot : snapshot;
                const bool restore = havePendingSnapshot || haveSnapshot;
                shapeObservation = requestedShape;
                clearInteractionState();
                haveSnapshot = false;
                havePendingSnapshot = false;
                if (restore) setSnapshot (latest);
                else repaint();
            }
            return;
        }
    }

    const auto markBounds = spectrum_geometry::markBoundsFor (outerPlot, scale);
    if (! absoluteObservation
        && focusFrequencyHz <= 0.0f && hoverNormalisedX < 0.0f
        && markBounds.contains (event.position))
    {
        if (haveMark && spectrum_geometry::markClearBoundsFor (
                markBounds, scale).contains (event.position))
        {
            markedDelta.fill (0.0f);
            markedDeltaValid.fill (0u);
            haveMark = false;
        }
        else if (haveSnapshot && currentSnapshotValid()
                 && std::any_of (pendingDeltaValid.begin(), pendingDeltaValid.end(),
                                 [] (uint8_t valid) { return valid != 0u; }))
        {
            markedDelta = pendingDelta;
            markedDeltaValid = pendingDeltaValid;
            haveMark = true;
        }
        else
        {
            modeActionNotice = "MARK --";
            modeActionNoticeUntilMs = juce::Time::getMillisecondCounterHiRes() + 1'500.0;
        }
        repaint();
        return;
    }

    if (! currentSnapshotValid())
        return;
    const bool expanded = scale > 1.1f;
    if (focusFrequencyHz > 0.0f)
    {
        auto readout = midSideObservation
            ? spectrum_geometry::midSideReadoutBoundsFor (outerPlot, scale, expanded)
            : spectrum_geometry::readoutBoundsFor (outerPlot, scale, expanded, true);
        if (spectrum_geometry::focusClearBoundsFor (
                readout, scale).contains (event.position))
        {
            focusFrequencyHz = -1.0f;
            hoverNormalisedX = -1.0f;
            repaint();
            return;
        }
    }
    const float controlsBottom = outerPlot.getY()
        + (float) (ui_contract::spectrumChannelModeTop
                 + ui_contract::spectrumChannelModeHeight) * scale;
    if (! plot.contains (event.position) || event.position.y < controlsBottom)
        return;
    const float normalised = spectrum_geometry::clampToBandCentreRange (
        juce::jlimit (0.0f, 1.0f,
            (event.position.x - plot.getX()) / plot.getWidth()));
    focusFrequencyHz = spectrum_geometry::frequencyForProbeNormalisedX (
        normalised, snapshot.min_hz, snapshot.max_hz);
    hoverNormalisedX = normalised;
    repaint();
}
}
