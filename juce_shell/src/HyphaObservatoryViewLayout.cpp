#include "HyphaObservatoryView.h"
namespace hypha::observatory
{
namespace {
juce::Rectangle<int> toJuce (Rect r) { return { r.x, r.y, r.width, r.height }; }
}
void View::resized()
{
    levelHistoryArea = {};
    levelHistoryPointer.reset();
    hoveredLevelHistoryIndex.reset();
    const auto preset = currentPreset();
    const auto context = presentationContext();
    for (auto* button : { &levelButton, &timeButton, &frequencyButton, &spaceButton,
                          &referenceButton, &domainCycleButton, &targetButton, &deltaButton,
                          &timeRangeButton, &compactLoudnessButton, &compactRangeButton,
                          &contextButton, &scaleButton, &sizeButton, &operationsButton,
                          &stopButton, &guideButton, &statusButton, &hybridVuButton,
                          &clearPeakClipButton, &resetButton, &noteButton, &captureButton,
                          &localBlindButton })
        button->setPresentationContext (context);
    const auto layout = shellLayout (role, preset, guidePresence());
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
                              &timeRangeButton, &compactLoudnessButton, &compactRangeButton,
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
    frequencyButton.setVisible (! singleDomainControl
                                && domainCapabilities (role).frequency);
    spaceButton.setVisible (! singleDomainControl);
    referenceButton.setVisible (! singleDomainControl
                                && domainCapabilities (role).reference);
    if (singleDomainControl)
        domainCycleButton.setBounds (navigation);
    else
    {
        auto remaining = navigation;
        const auto domainCount = domainCapabilities (role).reference ? 5 : 3;
        const auto width = remaining.getWidth() / domainCount;
        levelButton.setBounds (remaining.removeFromLeft (width));
        timeButton.setBounds (remaining.removeFromLeft (width));
        if (domainCapabilities (role).frequency)
            frequencyButton.setBounds (remaining.removeFromLeft (width));
        spaceButton.setBounds (remaining.removeFromLeft (width));
        if (domainCapabilities (role).reference)
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
    timeRangeButton.setVisible (capabilities().historyRange && ! captureFrame);
    scaleButton.setVisible (capabilities().loudnessScale && ! captureFrame);
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
        timeRangeButton.setBounds (
            controls.removeFromRight (juce::jmax (120, timeRangeWidth (density))).reduced (2, 2));
    }
    const bool compactLevel = compact && selectedDomain == Domain::level;
    compactLoudnessButton.setVisible (false);
    compactRangeButton.setVisible (
        compactLevel && target() == ObservationTarget::absolute);
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
    statusButton.setVisible (! captureFrame && feedbackText.isNotEmpty());
    statusButton.setBounds (sessionArea.reduced (1, 2));
    layoutFooterActions (toJuce (layout.actions));
}
}
