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
    }
    return "LEVEL";
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> area,
                ExperienceFamily family, float corner = 4.0f)
{
    const auto opacity = family == ExperienceFamily::compactMeter ? 0.96f : 0.76f;
    g.setColour (BG.withAlpha (opacity));
    g.fillRoundedRectangle (area.toFloat(), corner);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), corner, 1.0f);
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
                          &domainCycleButton, &targetButton, &deltaButton, &timeRangeButton,
                          &compactLoudnessButton, &compactRangeButton,
                          &sizeButton, &resetButton, &captureButton })
    {
        styleButton (*button);
        addAndMakeVisible (*button);
    }
    levelButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::level); };
    timeButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::time); };
    frequencyButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::frequency); };
    spaceButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::space); };
    domainCycleButton.onClick = [this] { cycleDomain(); };
    targetButton.onClick = [this]
    {
        if (currentPreset().density == Density::observatory)
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
    compactLoudnessButton.setTooltip ("Switch Momentary / Short-term loudness");
    compactRangeButton.setTooltip ("Switch current / session maximum values");
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

void View::setDisplayedEditorSize (int width, int height)
{
    if (! validEditorSize (width, height))
        return;
    displayedEditorWidth = width;
    displayedSizeLabel = juce::String (juce::roundToInt (
        static_cast<double> (width) * 100.0 / 300.0)) + "%";
    sizeButton.setButtonText (displayedSizeLabel);
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
    const auto next = nextDomain (role, selectedDomain);
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
    domainCycleButton.setToggleState (true, juce::dontSendNotification);
    domainCycleButton.setButtonText (domainName (selectedDomain));
    const bool fullCockpit = currentPreset().density == Density::observatory;
    targetButton.setButtonText (
        fullCockpit ? "POST"
                    : target() == ObservationTarget::absolute ? "POST" : hypha::delta());
    targetButton.setToggleState (
        fullCockpit ? target() == ObservationTarget::absolute
                    : target() == ObservationTarget::delta,
        juce::dontSendNotification);
    targetButton.setEnabled (selectedDomain != Domain::space);
    deltaButton.setToggleState (target() == ObservationTarget::delta,
                                juce::dontSendNotification);
    deltaButton.setEnabled (selectedDomain != Domain::space);
    timeRangeButton.setButtonText (historyRequest().label);
    compactLoudnessButton.setButtonText (
        selectedShortTermLoudness ? "LOUDNESS S" : "LOUDNESS M");
    compactLoudnessButton.setToggleState (true, juce::dontSendNotification);
    compactRangeButton.setButtonText (compactShowsMaximum ? "MAX" : "CURRENT");
    compactRangeButton.setToggleState (true, juce::dontSendNotification);
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
    const auto navigation = toJuce (layout.domainNavigation);
    domainCycleButton.setVisible (singleDomainControl);
    levelButton.setVisible (! singleDomainControl);
    timeButton.setVisible (! singleDomainControl);
    frequencyButton.setVisible (! singleDomainControl
                                && domainCapabilities (role).frequency);
    spaceButton.setVisible (! singleDomainControl);
    if (singleDomainControl)
        domainCycleButton.setBounds (navigation);
    else
    {
        auto remaining = navigation;
        const auto domainCount = domainCapabilities (role).frequency ? 4 : 3;
        const auto width = remaining.getWidth() / domainCount;
        levelButton.setBounds (remaining.removeFromLeft (width));
        timeButton.setBounds (remaining.removeFromLeft (width));
        if (domainCapabilities (role).frequency)
            frequencyButton.setBounds (remaining.removeFromLeft (width));
        spaceButton.setBounds (remaining);
    }

    const bool splitTargets = role == Role::post
                           && preset.density == Density::observatory;
    targetButton.setVisible (role == Role::post);
    deltaButton.setVisible (splitTargets);
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
    if (timeRangeButton.isVisible())
    {
        auto timeRangeArea = bodyArea;
        timeRangeButton.setBounds (
            timeRangeArea.removeFromTop (juce::jmin (22, timeRangeArea.getHeight() / 5))
                         .removeFromRight (juce::jmin (110, timeRangeArea.getWidth() / 2)));
    }
    const bool compactLevel = compact && selectedDomain == Domain::level;
    compactLoudnessButton.setVisible (compactLevel);
    compactRangeButton.setVisible (
        compactLevel && target() == ObservationTarget::absolute);
    if (compactLevel)
    {
        auto compactBody = bodyArea;
        auto compactControls = compactBody.removeFromTop (20);
        compactLoudnessButton.setBounds (
            compactControls.removeFromLeft (juce::jmin (82, compactControls.getWidth() / 2))
                           .reduced (2, 1));
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
    const bool full = captureEntryAvailable (role, preset);
    resetButton.setVisible (! captureFrame);
    captureButton.setVisible (full && ! captureFrame);
    if (full && ! captureFrame)
    {
        auto split = actions;
        resetButton.setBounds (split.removeFromLeft (split.getWidth() / 2).reduced (1, 2));
        captureButton.setBounds (split.reduced (1, 2));
    }
    else if (! captureFrame)
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
        paintLevelCapture (g, bodyArea);
    else if (selectedDomain == Domain::level) paintLevel (g, bodyArea);
    else if (selectedDomain == Domain::time) paintTime (g, bodyArea);
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
                           : density == Density::standard ? 16.0f : 18.0f;
    auto titleArea = toJuce (layout.roleTitle).reduced (6, 0);
    const int roleWidth = density == Density::compact ? 26 : 34;
    auto roleArea = titleArea.removeFromRight (roleWidth);
    g.setFont (labelFont (titleHeight));
    g.setColour (COL_NORMAL);
    g.drawFittedText ("HYPHA", titleArea, juce::Justification::centredLeft, 1, 0.82f);
    g.setColour (COL_MUTED.brighter (0.18f));
    g.setFont (labelFont (titleHeight * 0.78f));
    g.drawText (role == Role::post ? "POST" : "PRE", roleArea,
                juce::Justification::centredRight);
    g.setColour (connectionColour);
    g.setFont (monoFont (currentPreset().density == Density::compact ? 9.0f : 11.0f));
    if (contract.hyphaAperture)
        statusArea.removeFromLeft (22);
    g.drawText (connectionText, statusArea.reduced (4, 0),
                juce::Justification::centredRight);
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

void View::paintFooter (juce::Graphics& g, const ShellLayout& layout)
{
    drawPanel (g, toJuce (layout.footer), experienceFamily(), 4.0f);
    auto session = sessionArea.reduced (6, 0);
    const auto& meter = observatoryFrame.meter;
    const auto state = ! frameAvailable ? juce::String ("SESSION —")
                     : meter.state == KIRIN_METER_SESSION_EMPTY ? juce::String ("READY  ")
                     : observatoryFrame.signal_state == KIRIN_SIGNAL_STATE_BYPASSED
                         ? juce::String ("BYPASSED  ")
                     : observatoryFrame.signal_state == KIRIN_SIGNAL_STATE_INACTIVE
                         ? juce::String ("INACTIVE  ")
                     : juce::String ("ACTIVE  ");
    const auto seconds = frameAvailable && meter.sample_rate > 0
        ? static_cast<double> (meter.active_frames) / static_cast<double> (meter.sample_rate) : 0.0;
    g.setColour (frameAvailable ? COL_MUTED.brighter (0.25f) : COL_MUTED);
    if (! captureFrame)
    {
        g.setFont (monoFont (currentPreset().density == Density::compact ? 8.5f : 10.5f));
        g.drawText (state + juce::String (seconds, 1) + " S", session,
                    juce::Justification::centred);
        return;
    }

    auto upper = session.removeFromTop (session.getHeight() / 2);
    auto lower = session;
    auto provenance = captureTimestamp;
    if (captureVersion.isNotEmpty())
        provenance += "  |  v" + captureVersion;
    g.setFont (monoFont (8.0f));
    auto statusArea = upper.removeFromLeft (juce::roundToInt (upper.getWidth() * 0.55f));
    g.drawFittedText (state + juce::String (seconds, 1) + " S  |  ITU-R BS.1770",
                      statusArea, juce::Justification::centredLeft, 1, 0.78f);
    g.drawFittedText (provenance, upper, juce::Justification::centredRight, 1, 0.72f);
    const auto metadata = captureMetadata.footerLine();
    if (metadata.isNotEmpty())
    {
        g.setColour (COL_FLORA.withAlpha (0.82f));
        g.setFont (monoFont (7.5f));
        g.drawFittedText (metadata, lower, juce::Justification::centredLeft, 1, 0.70f);
    }
}

void View::paintTime (juce::Graphics& g, juce::Rectangle<int> area)
{
    const bool compact = experienceFamily() == ExperienceFamily::compactMeter;
    if (! compact)
    {
        auto context = area.removeFromTop (22);
        g.setColour (COL_MUTED.withAlpha (0.78f));
        g.setFont (monoFont (8.0f));
        g.drawText ("SESSION HISTORY", context.reduced (6, 0),
                    juce::Justification::centredLeft);
        area.removeFromTop (2);
    }
    time_history::paint (g, area, history, compact ? historyRequest().label : "",
                         target() == ObservationTarget::delta, compact);
}
}
