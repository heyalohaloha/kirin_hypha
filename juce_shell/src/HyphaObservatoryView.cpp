#include "HyphaObservatoryView.h"
#include "HyphaHybridVuPainter.h"
#include "HyphaSpacePainter.h"
#include "HyphaTimeHistoryPainter.h"
#include "HyphaObservationEquality.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
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
    surface_material::paintPanel (g, area.toFloat(), opacity, corner);
}
void styleButton (juce::TextButton& button)
{
    button.setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
}

View::View (Role roleIn) : role (roleIn)
{
    setOpaque (true);
    addAndMakeVisible (informationButton);
    informationButton.onClick = [this] { if (onInformation) onInformation(); };
    for (auto* button : { &levelButton, &timeButton, &frequencyButton, &spaceButton,
                          &referenceButton,
                          &domainCycleButton, &targetButton, &deltaButton, &timeRangeButton,
                          &compactLoudnessButton, &compactRangeButton,
                          &contextButton, &scaleButton, &sizeButton, &operationsButton,
                          &stopButton, &guideButton, &statusButton, &hybridVuButton,
                          &clearPeakClipButton, &resetButton, &noteButton, &captureButton })
    {
        styleButton (*button);
        addAndMakeVisible (*button);
    }
    levelButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::level); };
    timeButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::time); };
    frequencyButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::frequency); };
    spaceButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::space); };
    referenceButton.onClick = [this] { if (onDomainChange) onDomainChange (Domain::reference); };
    referenceButton.setComponentID ("observatory-reference");
    setReferenceOwned (false);
    domainCycleButton.onClick = [this]
    { if (onDomainMenu) onDomainMenu(); else cycleDomain(); };
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
        if (onContextMenu) { onContextMenu(); return; }
        const auto next = meter_context::nextContext (selectedMeterContext);
        if (onContextChange) onContextChange (next); else setMeterContext (next);
    };
    scaleButton.onClick = [this]
    {
        const auto next = meter_context::nextScale (selectedScaleMode);
        if (onScaleChange) onScaleChange (next); else setScaleMode (next);
    };
    compactLoudnessButton.setTooltip ("Switch Momentary / Short-term loudness"); compactRangeButton.setTooltip ("Switch current / session maximum values");
    contextButton.setComponentID ("observatory-meter-context");
    contextButton.setTooltip ("Choose whether this instance observes a mix bus or a track / stem"); scaleButton.setTooltip ("Switch WIDE / FOCUS loudness scale");
    sizeButton.onClick = [this] { if (onSizeMenu) onSizeMenu(); else cycleSize(); };
    sizeButton.setTooltip ("Choose an exact editor size");
    operationsButton.setTooltip ("Keep, measurement, and display controls");
    operationsButton.setComponentID ("observatory-menu");
    operationsButton.onClick = [this] { if (onOperationsMenu) onOperationsMenu(); };
    stopButton.setColour (juce::TextButton::textColourOffId, COL_FLORA_BR);
    stopButton.setTooltip ("Stop the selected PRE / POST Keep");
    stopButton.onClick = [this] { if (onStop) onStop(); };
    guideButton.setColour (juce::TextButton::textColourOffId, COL_GUIDE_BR);
    guideButton.setTooltip ("Open the received Kirin OS Guide details");
    guideButton.onClick = [this] { if (onGuideDetails) onGuideDetails(); };
    statusButton.setColour (juce::TextButton::textColourOffId, COL_NORMAL);
    statusButton.onClick = [this] { if (onFeedbackDetails) onFeedbackDetails(); };
    hybridVuButton.setComponentID ("observatory-hybrid-vu");
    hybridVuButton.setTitle ("Hybrid VU");
    hybridVuButton.setDescription ("Show or hide the Hybrid VU without changing measurement");
    hybridVuButton.setTooltip (hybridVuButton.getDescription());
    hybridVuButton.onClick = [this]
    {
        toggleHybridVu();
        if (onHybridVuChange) onHybridVuChange (hybridVuVisible());
    };
    clearPeakClipButton.setComponentID ("observatory-clear-peak-clip");
    clearPeakClipButton.setTitle ("Clear True Peak and Clip holds");
    clearPeakClipButton.setDescription (
        "Clear held channel True Peak and Clip indicators; keep current values and history");
    clearPeakClipButton.setTooltip (clearPeakClipButton.getDescription());
    clearPeakClipButton.onClick = [this]
    {
        if (onClearPeakClipHolds) onClearPeakClipHolds();
    };
    resetButton.onClick = [this] { if (onReset) onReset(); };
    noteButton.onClick = [this] { if (onNote) onNote(); };
    noteButton.setComponentID ("observatory-note");
    captureButton.onClick = [this] { if (onCapture) onCapture(); };
    styleButton (localBlindButton);
    localBlindButton.setComponentID ("observatory-local-blind");
    localBlindButton.setTitle ("PRE / POST Blind Compare");
    localBlindButton.setDescription ("Capture and compare one exact four second PRE and POST range");
    localBlindButton.setTooltip (localBlindButton.getDescription());
    localBlindButton.onClick = [this] { if (onLocalBlind) onLocalBlind(); };
    addChildComponent (localBlindButton);
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
    runSummary = {};
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
    runSummary = {};
    levelHistoryPointer.reset();
    hoveredLevelHistoryIndex.reset();
    updateControls();
    resized();
    repaint();
}

void View::setDeltaTargetEnabled (bool enabled)
{
    if (deltaTargetEnabled == enabled)
        return;
    deltaTargetEnabled = enabled;
    updateControls();
}

void View::setConnection (juce::String text, juce::Colour colour, ConnectionState state)
{
    if (connectionText == text && connectionColour == colour && connectionState == state)
        return;
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
    if (watchDisplayAvailable == available && observation_equality::same (watchDisplay, display))
        return;
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
    if (guidePrimary == primary && guideDetail == detail && guideEmphasized == emphasized)
        return;
    const bool changedPresence = (guidePrimary.isNotEmpty() || guideDetail.isNotEmpty())
        != (primary.isNotEmpty() || detail.isNotEmpty());
    guidePrimary = std::move (primary);
    guideDetail = std::move (detail);
    guideEmphasized = emphasized;
    updateControls();
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
    updateControls();
    resized();
    repaint();
}

void View::setHistory (std::vector<KirinMeterHistoryEntry> entries)
{
    if (history.size() == entries.size()
        && std::equal (history.begin(), history.end(), entries.begin(),
                       [] (const auto& a, const auto& b) { return observation_equality::same (a, b); }))
        return;
    history = std::move (entries);
    runSummary = target() == ObservationTarget::absolute
        ? run_summary::summarize (history) : run_summary::Result {};
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
    runSummary = {};
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
    if (! capabilities().targetSelectable)
        targetButton.setButtonText (target() == ObservationTarget::absolute ? "POST" : hypha::delta());
    targetButton.setEnabled (capabilities().targetSelectable
                             && (fullCockpit || deltaTargetEnabled));
    targetButton.setTooltip (deltaTargetEnabled ? capabilities().help
                                                : "Select LR, MID, or SIDE to view Delta");
    deltaButton.setToggleState (target() == ObservationTarget::delta,
                                juce::dontSendNotification);
    deltaButton.setEnabled (capabilities().targetSelectable && deltaTargetEnabled);
    deltaButton.setTooltip (deltaTargetEnabled
        ? "POST minus PRE; select POST to return to absolute values"
        : "Select LR, MID, or SIDE to view Delta");
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
    const auto guideLabel = guidePrimary.containsIgnoreCase ("MASKING") ? "MASKING"
                          : guidePrimary.containsIgnoreCase ("INSPECT") ? "INSPECT"
                          : guidePrimary.containsIgnoreCase ("CONNECT") ? "CONNECT"
                                                                       : "OS GUIDE";
    guideButton.setButtonText (guideLabel);
    guideButton.setToggleState (guideEmphasized, juce::dontSendNotification);
    guideButton.setTooltip ((guidePrimary + "  " + guideDetail).trim());
    statusButton.setButtonText (feedbackText);
    statusButton.setTooltip (feedbackText);
    hybridVuButton.setToggleState (hybridVuVisible(), juce::dontSendNotification);
}


void View::paint (juce::Graphics& g)
{
    if (hybridVuVisible())
    {
        hybrid_vu::paint (g, getLocalBounds(), {
            role, observatoryFrame.meter, watchDisplay,
            currentFactsAvailable(), cumulativeFactsAvailable(), watchDisplayAvailable,
            selectedShortTermLoudness, hostRecording, connectionText, connectionColour,
            jungleAppearance, presentationContext()
        });
        if (feedbackText.isNotEmpty())
        {
            auto feedback = getLocalBounds();
            feedback = { feedback.getX(), juce::roundToInt (getHeight() * 0.880f),
                         feedback.getWidth(), juce::roundToInt (getHeight() * 0.095f) };
            feedback.removeFromLeft (juce::roundToInt (getWidth() * 0.18f));
            feedback.removeFromRight (juce::roundToInt (getWidth() * 0.20f));
            g.setColour (BG.withAlpha (0.92f));
            g.fillRoundedRectangle (feedback.toFloat(), 3.0f);
            g.setColour (COL_NORMAL);
            g.setFont (monoFont (presentationContext(), typography::TextRole::status));
            text_style::draw (g, feedbackText, feedback.reduced (3, 0),
                              presentationContext(), typography::TextRole::status,
                              juce::Justification::centred);
        }
        return;
    }
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
        background.drawDomainBed (g, bodyArea, state);
        background.drawHyphaSpecimen (g, bodyArea, state);
    }
    paintHeader (g, layout);
    if (selectedDomain == Domain::level && (captureFrame || fullCockpit()))
        paintLevelWithHistory (g, bodyArea);
    else if (selectedDomain == Domain::level) paintLevel (g, bodyArea);
    else if (selectedDomain == Domain::time && ! externalAnalysisBodyActive)
        paintTime (g, bodyArea);
    else if (selectedDomain == Domain::space)
        space_field::paint (g, bodyArea, observatoryFrame.meter,
                            currentFactsAvailable(),
                            contract.family == ExperienceFamily::compactMeter,
                            presentationContext());
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
    const auto context = presentationContext();
    auto titleArea = toJuce (layout.roleTitle).reduced (6, 0);
    const auto roleText = role == Role::post ? juce::String ("POST") : juce::String ("PRE");
    const auto roleFont = labelFont (context, typography::TextRole::shellTitle);
    const auto roleWidth = juce::jmin (
        titleArea.getWidth() - 20,
        juce::roundToInt (roleFont.getStringWidthFloat (roleText)) + 2);
    auto roleArea = titleArea.removeFromLeft (juce::jmax (1, roleWidth));
    titleArea.removeFromLeft (density == Density::compact ? 3 : 5);
    g.setFont (labelFont (context, typography::TextRole::shellTitle));
    g.setColour (role == Role::post ? COL_FLORA : COL_LED_BLUE);
    text_style::draw (g, roleText, roleArea, context, typography::TextRole::shellTitle,
                      juce::Justification::centredLeft);
    g.setColour (COL_NORMAL);
    g.setFont (labelFont (context, typography::TextRole::shellTitle));
    text_style::draw (g, "HYPHA",
                      titleArea.translated (0, density == Density::compact ? 1 : 0), context,
                      typography::TextRole::shellTitle, juce::Justification::centredLeft);
    if (! externalConnectionLabelVisible || captureFrame)
    {
        g.setColour (connectionColour);
        g.setFont (monoFont (context, typography::TextRole::status));
        if (contract.hyphaAperture)
            statusArea.removeFromLeft (22);
        g.drawText (connectionText, statusArea.reduced (4, 0),
                    juce::Justification::centredRight);
    }
}

}
