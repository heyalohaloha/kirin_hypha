#include "HyphaObservatoryView.h"
#include "HyphaSpacePainter.h"
#include "HyphaTimeHistoryPainter.h"
#include <utility>

namespace hypha::observatory
{
namespace
{
juce::Rectangle<int> toJuce (Rect value)
{
    return { value.x, value.y, value.width, value.height };
}
const char* domainName (Domain domain)
{
    switch (domain)
    {
        case Domain::level:     return "LEVEL";
        case Domain::time:      return "TIME";
        case Domain::frequency: return "FREQ";
        case Domain::space:     return "SPACE";
        case Domain::reference: return "REF";
    }
    return "LEVEL";
}
void drawPanel (juce::Graphics& g, juce::Rectangle<int> area,
                ExperienceFamily family, float corner = 4.0f)
{
    const auto opacity = family == ExperienceFamily::compactMeter ? 0.96f : 0.76f;
    g.setColour (BG.withAlpha (opacity));
    g.fillRoundedRectangle (area.toFloat(), corner);
    g.setColour (COL_MUTED.withAlpha (0.34f)); g.drawRoundedRectangle (area.toFloat().reduced (0.5f), corner, 1.0f);
}
void styleButton (juce::TextButton& button)
{
    button.setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
}

View::View (Role roleIn) : role (roleIn)
{
    setOpaque (true);
    for (auto* button : { &levelButton, &timeButton, &frequencyButton, &spaceButton,
                          &referenceButton,
                          &domainCycleButton, &targetButton, &deltaButton, &timeRangeButton,
                          &compactLoudnessButton, &compactRangeButton,
                          &contextButton, &scaleButton, &sizeButton, &resetButton, &captureButton })
    {
        styleButton (*button);
        addAndMakeVisible (*button);
    }
    levelButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::level); };
    timeButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::time); };
    frequencyButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::frequency); };
    spaceButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::space); };
    referenceButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::reference); };
    domainCycleButton.onClick = [this] { cycleDomain(); };
    targetButton.onClick = [this]
    {
        if (isFullDensity (currentPreset().density))
        {
            if (onTargetChange) onTargetChange (ObservationTarget::absolute);
            return;
        }
        const auto next = selectedTarget == ObservationTarget::absolute
            ? ObservationTarget::delta : ObservationTarget::absolute;
        if (onTargetChange) onTargetChange (next);
    };
    deltaButton.onClick = [this]
    {
        if (onTargetChange) onTargetChange (ObservationTarget::delta);
    };
    timeRangeButton.onClick = [this] { cycleTimeRange(); };
    compactLoudnessButton.onClick = [this]
    {
        const auto next = ! selectedShortTermLoudness;
        if (onLoudnessChange) onLoudnessChange (next); else setShortTermLoudness (next);
    };
    compactRangeButton.onClick = [this] { setCompactMaximum (! compactShowsMaximum); };
    contextButton.onClick = [this]
    {
        const auto next = meter_context::nextContext (selectedMeterContext);
        if (onContextChange) onContextChange (next); else setMeterContext (next);
    };
    scaleButton.onClick = [this]
    {
        const auto next = meter_context::nextScale (selectedScaleMode);
        if (onScaleChange) onScaleChange (next); else setScaleMode (next);
    };
    compactLoudnessButton.setTooltip ("Switch Momentary / Short-term loudness"); compactRangeButton.setTooltip ("Switch current / session maximum values");
    contextButton.setTooltip ("Switch TRACK/STEM / 2MIX meter context"); scaleButton.setTooltip ("Switch WIDE / FOCUS loudness scale");
    sizeButton.onClick = [this] { cycleSize(); };
    resetButton.onClick = [this] { if (onReset) onReset(); };
    captureButton.onClick = [this] { if (onCapture) onCapture(); };
    updateControls();
}

void View::setDomain (Domain value)
{
    value = sanitizeDomain (role, value);
    if (selectedDomain == value)
        return;
    selectedDomain = value;
    levelHistoryPointer.reset();
    hoveredLevelHistoryIndex.reset();
    levelHistoryArea = {};
    updateControls();
    resized();
    repaint();
}

void View::setTimeRange (TimeRange value)
{
    if (timeRange == value)
        return;
    timeRange = value;
    history.clear();
    levelHistoryPointer.reset();
    hoveredLevelHistoryIndex.reset();
    updateControls();
    repaint (bodyArea);
}

void View::setTarget (ObservationTarget value)
{
    if (! targetAllowed (role, value) || selectedTarget == value)
        return;
    selectedTarget = value;
    history.clear();
    levelHistoryPointer.reset();
    hoveredLevelHistoryIndex.reset();
    updateControls();
    resized();
    repaint();
}

void View::setConnection (juce::String text, juce::Colour colour, ConnectionState state)
{
    connectionText = std::move (text);
    connectionColour = colour;
    connectionState = state;
    repaint();
}

void View::setExternalConnectionLabelVisible (bool visible)
{
    if (externalConnectionLabelVisible == visible)
        return;
    externalConnectionLabelVisible = visible;
    repaint (connectionArea);
}

void View::setWatchDisplay (const KirinWatchDisplay& display, bool available)
{
    watchDisplay = display;
    watchDisplayAvailable = available;
    if (selectedDomain == Domain::level)
        repaint (bodyArea);
}

void View::setShortTermLoudness (bool shortTerm)
{
    if (selectedShortTermLoudness == shortTerm)
        return;
    selectedShortTermLoudness = shortTerm;
    updateControls();
    repaint (bodyArea);
}

void View::setCompactMaximum (bool maximum)
{
    if (compactShowsMaximum == maximum)
        return;
    compactShowsMaximum = maximum;
    updateControls();
    repaint (bodyArea);
}

void View::setGuide (juce::String primary, juce::String detail, bool emphasized)
{
    const bool changedPresence = guidePrimary.isEmpty() && primary.isNotEmpty();
    guidePrimary = std::move (primary);
    guideDetail = std::move (detail);
    guideEmphasized = emphasized;
    if (changedPresence)
        resized();
    repaint();
}

void View::clearGuide()
{
    if (guidePrimary.isEmpty() && guideDetail.isEmpty())
        return;
    guidePrimary.clear();
    guideDetail.clear();
    guideEmphasized = false;
    resized();
    repaint();
}

void View::setHistory (std::vector<KirinMeterHistoryEntry> entries)
{
    history = std::move (entries);
    refreshLevelHistoryHover();
    if (selectedDomain == Domain::time
        || (selectedDomain == Domain::level && fullCockpit()))
        repaint (bodyArea);
}

View::HistoryRequest View::historyRequest() const noexcept
{
    const auto maxOutput = static_cast<size_t> (juce::jlimit (
        128, 1'200, juce::jmax (1, bodyArea.getWidth()) * 2));
    switch (timeRange)
    {
        case TimeRange::seconds30: return { KIRIN_METER_HISTORY_10_HZ, 300, maxOutput, "30 S / 10 HZ" };
        case TimeRange::minutes2:  return { KIRIN_METER_HISTORY_10_HZ, 1'200, maxOutput, "2 MIN / 10 HZ" };
        case TimeRange::minutes10: return { KIRIN_METER_HISTORY_10_HZ, 6'000, maxOutput, "10 MIN / 10 HZ" };
        case TimeRange::hours2:    return { KIRIN_METER_HISTORY_1_HZ, 7'200, maxOutput, "2 H / 1 HZ" };
        case TimeRange::hours24:   return { KIRIN_METER_HISTORY_0_1_HZ, 8'640, maxOutput, "24 H / 0.1 HZ" };
    }
    return {};
}

GuidePresence View::guidePresence() const noexcept
{
    return (guidePrimary.isNotEmpty() || guideDetail.isNotEmpty())
        ? GuidePresence::present : GuidePresence::absent;
}

void View::cycleDomain()
{
    auto next = nextDomain (role, selectedDomain);
    if (! referenceEnabled && next == Domain::reference) next = nextDomain (role, next);
    if (onDomainChange) onDomainChange (next);
}

void View::cycleTimeRange()
{
    timeRange = timeRange == TimeRange::seconds30 ? TimeRange::minutes2
              : timeRange == TimeRange::minutes2 ? TimeRange::minutes10
              : timeRange == TimeRange::minutes10 ? TimeRange::hours2
              : timeRange == TimeRange::hours2 ? TimeRange::hours24 : TimeRange::seconds30;
    if (onTimeRangeChange)
        onTimeRangeChange (timeRange);
    updateControls();
    history.clear();
    repaint (bodyArea);
}

void View::updateControls()
{
    levelButton.setToggleState (selectedDomain == Domain::level, juce::dontSendNotification);
    timeButton.setToggleState (selectedDomain == Domain::time, juce::dontSendNotification);
    frequencyButton.setToggleState (selectedDomain == Domain::frequency, juce::dontSendNotification);
    spaceButton.setToggleState (selectedDomain == Domain::space, juce::dontSendNotification);
    referenceButton.setToggleState (selectedDomain == Domain::reference, juce::dontSendNotification);
    domainCycleButton.setToggleState (true, juce::dontSendNotification);
    domainCycleButton.setButtonText (domainName (selectedDomain));
    const bool fullCockpit = isFullDensity (currentPreset().density);
    targetButton.setButtonText (
        fullCockpit ? "POST"
                    : target() == ObservationTarget::absolute ? "POST" : hypha::delta());
    targetButton.setToggleState (
        fullCockpit ? target() == ObservationTarget::absolute
                    : target() == ObservationTarget::delta,
        juce::dontSendNotification);
    targetButton.setEnabled (selectedDomain != Domain::space
                             && selectedDomain != Domain::reference);
    deltaButton.setToggleState (target() == ObservationTarget::delta,
                                juce::dontSendNotification);
    deltaButton.setEnabled (selectedDomain != Domain::space
                            && selectedDomain != Domain::reference);
    timeRangeButton.setButtonText (historyRequest().label);
    compactLoudnessButton.setButtonText (
        selectedShortTermLoudness ? "LOUDNESS S" : "LOUDNESS M");
    compactLoudnessButton.setToggleState (true, juce::dontSendNotification);
    compactRangeButton.setButtonText (compactShowsMaximum ? "MAX" : "CURRENT");
    compactRangeButton.setToggleState (true, juce::dontSendNotification);
    contextButton.setButtonText (selectedMeterContext == meter_context::MeterContext::trackStem
        ? (currentPreset().density == Density::compact ? "TRACK" : "TRACK/STEM") : "2MIX");
    contextButton.setToggleState (true, juce::dontSendNotification);
    scaleButton.setButtonText (
        selectedScaleMode == meter_context::ScaleMode::wide ? "WIDE" : "FOCUS");
    scaleButton.setToggleState (true, juce::dontSendNotification);
    sizeButton.setButtonText (
        displayedEditorWidth > 0 ? displayedSizeLabel : currentPreset().label);
}

void View::resized()
{
    levelHistoryArea = {};
    levelHistoryPointer.reset();
    hoveredLevelHistoryIndex.reset();
    const auto preset = currentPreset();
    const auto layout = shellLayout (role, preset, guidePresence());
    sizeButton.setButtonText (preset.label);
    bodyArea = toJuce (layout.body);
    connectionArea = toJuce (layout.connectionStatus);
    guideArea = toJuce (layout.guideRail);
    sessionArea = toJuce (layout.session);
    updateControls();
    if (captureFrame)
        sessionArea.setRight (toJuce (layout.footer).getRight());
    const auto contract = presentationContract (preset);
    const auto compact = contract.family == ExperienceFamily::compactMeter;
    const auto singleDomainControl = ! contract.domainTabs;
    auto navigation = toJuce (layout.domainNavigation);
    contextButton.setVisible (! captureFrame);
    if (contextButton.isVisible())
    {
        const auto contextWidth = preset.density == Density::compact ? 60
                                : preset.density == Density::focused ? 82 : 94;
        contextButton.setBounds (navigation.removeFromRight (
            juce::jmin (contextWidth, navigation.getWidth() / 2)).reduced (1, 2));
        navigation.removeFromRight (preset.density == Density::compact ? 1 : 3);
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
    deltaButton.setVisible (splitTargets && ! reference);
    auto targetArea = toJuce (layout.observationTarget);
    if (splitTargets)
    {
        targetButton.setBounds (
            targetArea.removeFromLeft (juce::roundToInt (targetArea.getWidth() * 0.62f))
                      .reduced (0, 2));
        targetArea.removeFromLeft (4);
        deltaButton.setBounds (targetArea.reduced (0, 2));
    }
    else
        targetButton.setBounds (targetArea.reduced (0, 2));
    timeRangeButton.setVisible (selectedDomain == Domain::time && contract.detailedAxes);
    scaleButton.setVisible (selectedDomain == Domain::time && ! captureFrame);
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
        controls.removeFromRight (timeScaleWidth (density));
        timeRangeButton.setBounds (
            controls.removeFromRight (timeRangeWidth (density)).reduced (2, 2));
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
    {
        const auto sizeWidth = compact ? 42 : 52;
        sizeButton.setBounds (sessionArea.removeFromRight (sizeWidth).reduced (1, 2));
    }
    const auto actions = toJuce (layout.actions);
    const bool full = captureEntryAvailable (role, preset) && ! reference;
    resetButton.setVisible (! captureFrame && ! reference);
    captureButton.setVisible (full && ! captureFrame);
    if (full && ! captureFrame)
    {
        auto split = actions;
        resetButton.setBounds (split.removeFromLeft (split.getWidth() / 2).reduced (1, 2));
        captureButton.setBounds (split.reduced (1, 2));
    }
    else if (! captureFrame && ! reference)
        resetButton.setBounds (actions.reduced (1, 2));
}

void View::paint (juce::Graphics& g)
{
    const auto state = worldState();
    const auto contract = presentation();
    if (contract.worldBackdrop)
        background.draw (g, getLocalBounds(), state);
    else
    {
        g.setColour (BG);
        g.fillRect (getLocalBounds());
    }
    const auto layout = shellLayout (role, currentPreset(), guidePresence());
    if (contract.domainWorld)
    {
        observatory_world::paintDomainBed (g, bodyArea, state);
        background.drawHyphaSpecimen (g, bodyArea, state);
    }
    paintHeader (g, layout);
    paintGuide (g, layout);
    if (selectedDomain == Domain::level && (captureFrame || fullCockpit()))
        paintLevelWithHistory (g, bodyArea);
    else if (selectedDomain == Domain::level) paintLevel (g, bodyArea);
    else if (selectedDomain == Domain::time && ! externalAnalysisBodyActive)
        paintTime (g, bodyArea);
    else if (selectedDomain == Domain::space)
        space_field::paint (g, bodyArea, observatoryFrame.meter,
                            currentFactsAvailable(),
                            contract.family == ExperienceFamily::compactMeter);
    else drawPanel (g, bodyArea, contract.family);
    paintFooter (g, layout);
    observatory_world::paintPlateFrame (g, getLocalBounds(), state);
}

void View::paintHeader (juce::Graphics& g, const ShellLayout& layout)
{
    const auto contract = presentation();
    drawPanel (g, toJuce (layout.header), contract.family, 5.0f);
    const auto state = worldState();
    auto statusArea = toJuce (layout.connectionStatus);
    if (contract.hyphaAperture)
        observatory_world::paintHyphaAperture (g, statusArea, state, connectionColour);
    else
        observatory_world::paintPairRoot (g, statusArea, state, connectionColour);
    const auto density = currentPreset().density;
    const auto titleHeight = density == Density::compact ? 12.0f
                           : density == Density::focused ? 14.0f
                           : density == Density::standard ? 16.0f
                           : density == Density::inspection ? 23.0f : 18.0f;
    auto titleArea = toJuce (layout.roleTitle).reduced (6, 0);
    const auto productFont = labelFont (titleHeight);
    const auto productWidth = juce::jmin (
        titleArea.getWidth() - 20,
        juce::roundToInt (productFont.getStringWidthFloat ("HYPHA")) + 2);
    auto productArea = titleArea.removeFromLeft (juce::jmax (1, productWidth));
    titleArea.removeFromLeft (density == Density::compact ? 3 : 5);
    g.setFont (productFont);
    g.setColour (COL_NORMAL);
    g.drawFittedText ("HYPHA", productArea, juce::Justification::centredLeft, 1, 0.82f);
    g.setColour (COL_MUTED.brighter (0.18f));
    g.setFont (labelFont (titleHeight * 0.88f));
    g.drawFittedText (role == Role::post ? "POST" : "PRE",
                      titleArea.translated (0, density == Density::compact ? 1 : 0),
                      juce::Justification::centredLeft, 1, 0.82f);
    if (! externalConnectionLabelVisible || captureFrame)
    {
        g.setColour (connectionColour);
        g.setFont (monoFont (density == Density::compact ? 9.0f
                           : density == Density::inspection ? 16.0f : 11.0f));
        if (contract.hyphaAperture)
            statusArea.removeFromLeft (22);
        g.drawText (connectionText, statusArea.reduced (4, 0),
                    juce::Justification::centredRight);
    }
}

void View::paintGuide (juce::Graphics& g, const ShellLayout& layout)
{
    if (! hasArea (layout.guideRail))
        return;
    auto area = toJuce (layout.guideRail);
    g.setColour ((guideEmphasized ? COL_GUIDE_BR : COL_GUIDE).withAlpha (0.10f));
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    g.setColour (guideEmphasized ? COL_GUIDE_BR : COL_GUIDE);
    g.fillRect (area.removeFromLeft (2));
    g.setFont (monoFont (9.5f));
    g.drawText (guidePrimary, area.removeFromLeft (juce::roundToInt (area.getWidth() * 0.58f))
                                  .reduced (6, 0), juce::Justification::centredLeft);
    g.setColour (COL_GUIDE.withAlpha (0.78f));
    g.drawText (guideDetail, area.reduced (4, 0), juce::Justification::centredRight);
    observatory_world::paintGuideRoot (g, toJuce (layout.guideRail), worldState());
}

}
