#include "HyphaObservatoryView.h"
namespace hypha::observatory
{
namespace {
juce::Rectangle<int> toJuce (Rect r) { return { r.x, r.y, r.width, r.height }; }

// Folded, a page without the POST / Δ target (Reference) gives the target's place, and the gap
// before it, to the domain cycle: size, actions and guide move right by that much.
ShellLayout withoutFoldedTarget (ShellLayout layout)
{
    const auto shift = layout.observationTarget.x + layout.observationTarget.width
                     - (layout.sizeSelector.x + layout.sizeSelector.width);
    for (auto* rect : { &layout.sizeSelector, &layout.actions, &layout.guideRail, &layout.session })
        rect->x += shift;
    layout.domainNavigation.width += shift;
    layout.observationTarget.width = 0;
    return layout;
}
}
void View::resized()
{
    levelHistoryArea = {};
    statusStrip = {};
    statusStripOverBody = false;
    levelHistoryPointer.reset();
    hoveredLevelHistoryIndex.reset();
    const auto preset = currentPreset();
    const auto context = presentationContext();
    for (auto* button : { &levelButton, &timeButton, &frequencyButton, &spaceButton,
                          &referenceButton, &domainCycleButton, &targetButton, &deltaButton,
                          &timeRangeButton, &timeRangeMenuButton, &compactLoudnessButton, &compactRangeButton,
                          &contextButton, &scaleButton, &sizeButton, &operationsButton,
                          &stopButton, &guideButton, &statusButton, &hybridVuButton,
                          &clearPeakClipButton, &resetButton, &noteButton, &captureButton,
                          &localBlindButton })
        button->setPresentationContext (context);
    auto layout = shellLayout (role, preset, guidePresence());
    if (footerFolds (preset.density) && layout.observationTarget.width > 0
        && selectedDomain == Domain::reference)
        layout = withoutFoldedTarget (layout);
    informationButton.setVisible (! captureFrame);
    informationButton.setBounds (toJuce (layout.roleTitle));
    sizeButton.setButtonText (preset.label);
    bodyArea = toJuce (layout.body);
    connectionArea = toJuce (layout.connectionStatus);
    guideArea = toJuce (layout.guideRail);
    sessionArea = toJuce (layout.session);
    updateControls();
    if (hybridVuVisible())
    {
        bodyArea = getLocalBounds();
        connectionArea = {};
        guideArea = {};
        sessionArea = {};
        for (auto* button : { &levelButton, &timeButton, &frequencyButton, &spaceButton,
                              &referenceButton, &domainCycleButton, &targetButton, &deltaButton,
                              &timeRangeButton, &timeRangeMenuButton, &compactLoudnessButton, &compactRangeButton,
                              &contextButton, &scaleButton, &sizeButton, &operationsButton,
                              &stopButton, &guideButton, &statusButton, &resetButton,
                              &noteButton, &captureButton, &localBlindButton })
            button->setVisible (false);
        const auto bounds = getLocalBounds();
        auto calibration = juce::Rectangle<int> (
            juce::roundToInt (bounds.getWidth() * 0.021f),
            juce::roundToInt (bounds.getHeight() * 0.880f),
            juce::roundToInt (bounds.getWidth() * 0.958f),
            juce::roundToInt (bounds.getHeight() * 0.095f));
        const auto buttonHeight = juce::jlimit (14, 30, calibration.getHeight() - 2);
        const auto vuWidth = juce::jlimit (42, 90,
                                           juce::roundToInt (bounds.getWidth() * 0.10f));
        const auto clearWidth = juce::jlimit (54, 112,
                                              juce::roundToInt (bounds.getWidth() * 0.14f));
        hybridVuButton.setVisible (true);
        hybridVuButton.setBounds (calibration.removeFromLeft (vuWidth)
                                      .withSizeKeepingCentre (vuWidth, buttonHeight));
        clearPeakClipButton.setVisible (true);
        clearPeakClipButton.setBounds (calibration.removeFromRight (clearWidth)
                                          .withSizeKeepingCentre (clearWidth, buttonHeight));
        return;
    }
    if (captureFrame)
        sessionArea.setRight (toJuce (layout.footer).getRight());
    const auto contract = presentationContract (preset);
    const auto compact = contract.family == ExperienceFamily::compactMeter;
    const auto singleDomainControl = ! contract.domainTabs;
    auto navigation = toJuce (layout.domainNavigation);
    // Folded footer: every status line (feedback, a running capture, WAITING and the like) goes to
    // the strip over the body's bottom edge; the cycle keeps its whole row.
    const bool folded = footerFolds (preset.density) && ! captureFrame;
    contextButton.setVisible (! captureFrame);
    if (contextButton.isVisible())
    {
        auto contextSelectorBounds = toJuce (layout.contextSelector);
        contextButton.setBounds (contextSelectorBounds.withSizeKeepingCentre (
            juce::jmin (136, contextSelectorBounds.getWidth()),
            contextSelectorBounds.getHeight()).reduced (1, 2));
    }
    domainCycleButton.setVisible (singleDomainControl);
    levelButton.setVisible (! singleDomainControl);
    timeButton.setVisible (! singleDomainControl);
    frequencyButton.setVisible (! measurementOnlySurround && ! singleDomainControl
                                && domainCapabilities (role).frequency);
    spaceButton.setVisible (! measurementOnlySurround && ! singleDomainControl);
    referenceButton.setVisible (! measurementOnlySurround && ! singleDomainControl
                                && domainCapabilities (role).reference);
    if (singleDomainControl)
        domainCycleButton.setBounds (navigation);
    else
    {
        auto remaining = navigation;
        const auto domainCount = measurementOnlySurround ? 2
            : domainCapabilities (role).reference ? 5 : 3;
        const auto width = remaining.getWidth() / domainCount;
        levelButton.setBounds (remaining.removeFromLeft (width));
        timeButton.setBounds (measurementOnlySurround ? remaining
                                                      : remaining.removeFromLeft (width));
        if (! measurementOnlySurround && domainCapabilities (role).frequency)
            frequencyButton.setBounds (remaining.removeFromLeft (width));
        if (! measurementOnlySurround)
            spaceButton.setBounds (remaining.removeFromLeft (width));
        if (! measurementOnlySurround && domainCapabilities (role).reference)
            referenceButton.setBounds (remaining);
    }

    const bool splitTargets = role == Role::post
                           && isFullDensity (preset.density);
    const bool reference = selectedDomain == Domain::reference;
    targetButton.setVisible (role == Role::post && ! reference);
    deltaButton.setVisible (splitTargets && ! reference && capabilities().targetSelectable);
    auto targetArea = toJuce (layout.observationTarget);
    if (deltaButton.isVisible())
    {
        targetButton.setBounds (
            targetArea.removeFromLeft (juce::roundToInt (targetArea.getWidth() * 0.62f))
                      .reduced (0, 2));
        targetArea.removeFromLeft (4);
        deltaButton.setBounds (targetArea.reduced (0, 2));
    }
    else
        targetButton.setBounds (targetArea.reduced (0, 2));
    timeRangeButton.setVisible (capabilities().historyRange && timeControlsShown());
    timeRangeMenuButton.setVisible (timeRangeButton.isVisible() && isFullDensity (preset.density));
    scaleButton.setVisible (capabilities().loudnessScale && timeControlsShown());
    if (scaleButton.isVisible())
    {
        const auto density = preset.density;
        auto available = bodyArea; auto controls = available.removeFromTop (timeNavigationHeight (density));
        scaleButton.setBounds (
            controls.removeFromRight (timeScaleWidth (density)).reduced (2, 2));
    }
    if (timeRangeButton.isVisible())
    {
        const auto density = preset.density;
        auto available = bodyArea; auto controls = available.removeFromTop (timeNavigationHeight (density));
        if (scaleButton.isVisible()) controls.removeFromRight (timeScaleWidth (density));
        auto range = controls.removeFromRight (juce::jmax (120, timeRangeWidth (density)));
        if (timeRangeMenuButton.isVisible())
            timeRangeMenuButton.setBounds (range.removeFromRight (28).reduced (2, 2));
        timeRangeButton.setBounds (range.reduced (2, 2));
    }
    const bool compactLevel = compact && selectedDomain == Domain::level;
    compactLoudnessButton.setVisible (false);
    // 100% is view-only: CURRENT / MAX is chosen at 125% and above.
    compactRangeButton.setVisible (compactLevel && preset.density != Density::compact
                                   && target() == ObservationTarget::absolute);
    if (compactLevel)
    {
        auto compactBody = bodyArea;
        auto compactControls = compactBody.removeFromTop (20);
        if (compactRangeButton.isVisible())
            compactRangeButton.setBounds (
                compactControls.removeFromRight (
                    juce::jmin (74, compactControls.getWidth())).reduced (2, 1));
    }
    sizeButton.setVisible (! captureFrame);
    if (! captureFrame)
        sizeButton.setBounds (toJuce (layout.sizeSelector).reduced (1, 2));
    guideButton.setVisible (guidePresence() == GuidePresence::present);
    guideButton.setBounds (guideArea.reduced (1, 2));
    auto footerActions = toJuce (layout.actions);
    if (reference && isFullDensity (preset.density))
    {
        footerActions.setLeft (footerActions.getRight() - (preset.density == Density::inspection ? 290 : 250));
        sessionArea.setRight (footerActions.getX() - 4);
    }
    statusStripOverBody = folded;
    statusStrip = folded ? bodyArea.withTop (bodyArea.getBottom() - statusStripHeight()) : sessionArea;
    statusButton.setVisible (! captureFrame && ! folded && feedbackText.isNotEmpty());
    statusButton.setBounds (sessionArea.reduced (1, 2));
    layoutFooterActions (footerActions);
}
}
