#include "HyphaSpectrumComponent.h"
#include "HyphaAnalysisUiText.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumUiContract.h"
#include "HyphaKeyLight.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"

#include <algorithm>

namespace hypha
{
SpectrumComponent::AverageButton::AverageButton (SpectrumComponent& component)
    : Button ("AVG LIVE"), owner (component)
{
    setComponentID ("spectrum-average");
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
}

bool SpectrumComponent::AverageButton::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::spaceKey))
        return Button::keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    return Button::keyPressed (key);
}

void SpectrumComponent::AverageButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const key_light::Scope light (*this);
    const auto scale = spectrum_geometry::visualScaleFor (owner.getLocalBounds().toFloat());
    const auto bounds = getLocalBounds().toFloat();
    surface_material::paintControl (g, bounds, over, down, true, COL_SPECTRUM_DELTA_BR, 3.0f * scale);
    g.setFont (monoFont (owner.presentationContext, typography::TextRole::navigation,
                         typography::Composition::visualization));
    g.setColour (COL_TEXT_SECONDARY);
    text_style::drawText (g, getButtonText(), bounds.toNearestInt(), juce::Justification::centred);
    if (hasKeyboardFocus (false))
    {
        g.setColour (COL_SPECTRUM_DELTA_BR);
        g.drawRoundedRectangle (bounds.reduced (1.0f), 3.0f * scale, 1.0f);
    }
}

void SpectrumComponent::resized()
{
    const auto bounds = getLocalBounds().toFloat();
    const auto scale = spectrum_geometry::visualScaleFor (bounds);
    const auto control = psbObservation ? juce::Rectangle<float>()
        : spectrum_geometry::averageBoundsFor (spectrum_geometry::plotBoundsFor (bounds), scale);
    averageButton.setBounds (control.toNearestInt());
    averageButton.setVisible (! control.isEmpty());
}

void SpectrumComponent::showAverageMenu()
{
    if (! averageButton.isVisible()) return;
    auto menu = averageMenu();
    menu.setLookAndFeel (&getLookAndFeel());
    const juce::Component::SafePointer<SpectrumComponent> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&averageButton),
        [safe] (int item) { if (safe != nullptr) safe->chooseAverage (item); });
}

void SpectrumComponent::mouseDown (const juce::MouseEvent& event)
{
    const auto bounds = getLocalBounds().toFloat();
    const float scale = spectrum_geometry::visualScaleFor (bounds);
    const auto outerPlot = spectrum_geometry::plotBoundsFor (bounds);
    if (spectrum_geometry::subviewBoundsFor (outerPlot, scale).contains (event.position))
    {
        psbObservation = ! psbObservation;
        resized();
        clearSnapshot();
        absolutePsbAvailable = deltaPsbAvailable = false;
        psbStatus = KIRIN_SPECTRUM_WARMING_UP;
        if (onSubviewChange) onSubviewChange();
        psbHoverBand = -1;
        hoverNormalisedX = -1.0f;
        setTooltip (psbObservation ? analysis_ui::psbOverviewTooltip (absoluteObservation)
                                   : "Spectrum: frequency level and difference");
        repaint();
        return;
    }
    if (psbObservation) return;
    const auto averageBounds = spectrum_geometry::averageBoundsFor (outerPlot, scale);
    if (averageBounds.contains (event.position))
    {
        showAverageMenu();
        return;
    }
    if (spectrum_geometry::lowBoundsFor (outerPlot, scale).contains (event.position))
    {
        if (onScopeChange && onScopeChange (! lowScope))
            setLowScope (! lowScope);
        return;
    }
    const auto plot = spectrum_geometry::dataPlotBoundsFor (
        bounds, ! absoluteObservation && focusFrequencyHz > 0.0f, displaySettings.rangeDb(), absoluteObservation);
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
            // Says what is missing: M/S needs the POST target, M/S and SIDE need a stereo input.
            const bool midSide = requestedMode == KIRIN_SPECTRUM_SELECTION_MID_SIDE;
            modeActionNotice = ! unavailable ? "MODE --"
                : inputChannels != 2u ? (midSide ? "M/S: stereo only" : "SIDE: stereo only")
                : "M/S: select POST";
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
    const float controlsBottom = outerPlot.getY() + (spectrum_geometry::viewOnly (scale) ? 0.0f
        : (float) (ui_contract::spectrumChannelModeTop + ui_contract::spectrumChannelModeHeight) * scale);
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
