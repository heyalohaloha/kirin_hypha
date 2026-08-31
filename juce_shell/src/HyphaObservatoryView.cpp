#include "HyphaObservatoryView.h"
#include "HyphaSpacePainter.h"
#include "HyphaTimeHistoryPainter.h"
#include <array>
#include <cmath>
#include <limits>
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

juce::String valueText (double value, int decimals = 1, bool signedValue = false)
{
    if (! std::isfinite (value))
        return "---";
    return (signedValue && value >= 0.0 ? "+" : "") + juce::String (value, decimals);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> area, float corner = 4.0f)
{
    g.setColour (BG.withAlpha (0.82f));
    g.fillRoundedRectangle (area.toFloat(), corner);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), corner, 1.0f);
}

void drawMetric (juce::Graphics& g,
                 juce::Rectangle<int> area,
                 const juce::String& label,
                 double value,
                 const juce::String& unit,
                 float valueHeight,
                 bool signedValue = false,
                 int decimals = 1,
                 const juce::String& textOverride = {})
{
    drawPanel (g, area);
    const auto labelArea = area.removeFromTop (juce::jmax (14, area.getHeight() / 4));
    g.setColour (COL_MUTED);
    g.setFont (labelFont (juce::jlimit (9.0f, 12.0f, valueHeight * 0.32f)));
    g.drawText (label, labelArea.reduced (6, 1), juce::Justification::centredLeft);
    if (area.getWidth() < 180)
    {
        g.setFont (labelFont (juce::jlimit (7.0f, 10.0f, valueHeight * 0.23f)));
        g.drawText (unit, labelArea.reduced (6, 1), juce::Justification::centredRight);
        g.setColour (std::isfinite (value) && textOverride.isEmpty() ? COL_NORMAL : COL_MUTED);
        drawTabularText (g, monoFont (valueHeight),
                         textOverride.isNotEmpty() ? textOverride
                                                   : valueText (value, decimals, signedValue),
                         area.reduced (5, 0).toFloat(), juce::Justification::centred);
        return;
    }
    const auto unitWidth = juce::jmin (46, area.getWidth() / 3);
    const auto unitArea = area.removeFromRight (unitWidth);
    g.setColour (std::isfinite (value) && textOverride.isEmpty() ? COL_NORMAL : COL_MUTED);
    drawTabularText (g, monoFont (valueHeight),
                     textOverride.isNotEmpty() ? textOverride
                                               : valueText (value, decimals, signedValue),
                     area.reduced (5, 0).toFloat(), juce::Justification::centredRight);
    g.setColour (COL_MUTED);
    g.setFont (labelFont (juce::jlimit (8.0f, 11.0f, valueHeight * 0.28f)));
    g.drawText (unit, unitArea.reduced (2, 0), juce::Justification::centredLeft);
}

double optionValue (double value, bool available)
{
    return available && std::isfinite (value)
        ? value : std::numeric_limits<double>::quiet_NaN();
}

void styleButton (juce::TextButton& button)
{
    button.setColour (juce::TextButton::buttonColourId, BG.withAlpha (0.76f));
    button.setColour (juce::TextButton::buttonOnColourId, kFieldFill.brighter (0.08f));
    button.setColour (juce::TextButton::textColourOffId, COL_MUTED);
    button.setColour (juce::TextButton::textColourOnId, COL_FLORA_BR);
    button.setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
}

View::View (Role roleIn) : role (roleIn)
{
    setOpaque (true);
    for (auto* button : { &levelButton, &timeButton, &frequencyButton, &spaceButton,
                          &domainCycleButton, &targetButton, &timeRangeButton,
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
        const auto next = selectedTarget == ObservationTarget::absolute
            ? ObservationTarget::delta : ObservationTarget::absolute;
        if (onTargetChange) onTargetChange (next);
    };
    timeRangeButton.onClick = [this] { cycleTimeRange(); };
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
    updateControls();
    repaint (bodyArea);
}

void View::setTarget (ObservationTarget value)
{
    if (! targetAllowed (role, value) || selectedTarget == value)
        return;
    selectedTarget = value;
    history.clear();
    updateControls();
    repaint();
}

void View::setConnectionText (juce::String text, juce::Colour colour)
{
    connectionText = std::move (text);
    connectionColour = colour;
    repaint();
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
    if (selectedDomain == Domain::time)
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
    repaint (bodyArea);
}

void View::updateControls()
{
    levelButton.setToggleState (selectedDomain == Domain::level, juce::dontSendNotification);
    timeButton.setToggleState (selectedDomain == Domain::time, juce::dontSendNotification);
    frequencyButton.setToggleState (selectedDomain == Domain::frequency, juce::dontSendNotification);
    spaceButton.setToggleState (selectedDomain == Domain::space, juce::dontSendNotification);
    domainCycleButton.setButtonText (domainName (selectedDomain));
    targetButton.setButtonText (target() == ObservationTarget::absolute ? "POST" : hypha::delta());
    targetButton.setToggleState (target() == ObservationTarget::delta, juce::dontSendNotification);
    targetButton.setEnabled (selectedDomain != Domain::space);
    timeRangeButton.setButtonText (historyRequest().label);
    sizeButton.setButtonText (currentPreset().label);
}

void View::resized()
{
    const auto preset = currentPreset();
    const auto layout = shellLayout (role, preset, guidePresence());
    sizeButton.setButtonText (preset.label);
    bodyArea = toJuce (layout.body);
    connectionArea = toJuce (layout.connectionStatus);
    guideArea = toJuce (layout.guideRail);
    sessionArea = toJuce (layout.session);
    const auto compact = preset.density == Density::compact;
    const auto singleDomainControl = compact || preset.density == Density::focused;
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

    targetButton.setVisible (role == Role::post);
    targetButton.setBounds (toJuce (layout.observationTarget).reduced (0, 2));
    timeRangeButton.setVisible (selectedDomain == Domain::time && ! compact);
    if (timeRangeButton.isVisible())
    {
        auto timeRangeArea = bodyArea;
        timeRangeButton.setBounds (
            timeRangeArea.removeFromTop (juce::jmin (22, timeRangeArea.getHeight() / 5))
                         .removeFromRight (juce::jmin (110, timeRangeArea.getWidth() / 2)));
    }
    sizeButton.setVisible (! captureFrame);
    if (! captureFrame)
    {
        const auto sizeWidth = compact ? 42 : 52;
        sizeButton.setBounds (sessionArea.removeFromRight (sizeWidth).reduced (1, 2));
    }
    const auto actions = toJuce (layout.actions);
    const bool full = preset.density == Density::observatory && role == Role::post;
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
    background.draw (g, getLocalBounds());
    const auto layout = shellLayout (role, currentPreset(), guidePresence());
    paintHeader (g, layout);
    paintGuide (g, layout);
    paintMeasuredMycelium (g, bodyArea);
    if (selectedDomain == Domain::level) paintLevel (g, bodyArea);
    else if (selectedDomain == Domain::time) paintTime (g, bodyArea);
    else if (selectedDomain == Domain::space)
        space_field::paint (g, bodyArea, observatoryFrame.meter, currentFactsAvailable());
    else drawPanel (g, bodyArea);
    paintFooter (g, layout);
}

void View::paintHeader (juce::Graphics& g, const ShellLayout& layout)
{
    drawPanel (g, toJuce (layout.header), 5.0f);
    g.setColour (COL_NORMAL);
    const auto density = currentPreset().density;
    const auto titleHeight = density == Density::compact ? 12.0f
                           : density == Density::focused ? 14.0f
                           : density == Density::standard ? 16.0f : 18.0f;
    g.setFont (labelFont (titleHeight));
    g.drawText (role == Role::post ? "HYPHA POST" : "HYPHA PRE",
                toJuce (layout.roleTitle).reduced (6, 0), juce::Justification::centredLeft);
    g.setColour (connectionColour);
    g.setFont (monoFont (currentPreset().density == Density::compact ? 9.0f : 11.0f));
    g.drawText (connectionText, toJuce (layout.connectionStatus).reduced (4, 0),
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
}

void View::paintFooter (juce::Graphics& g, const ShellLayout& layout)
{
    drawPanel (g, toJuce (layout.footer), 4.0f);
    const auto session = sessionArea.reduced (6, 0);
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
    g.setFont (monoFont (currentPreset().density == Density::compact ? 8.5f : 10.5f));
    auto captureFacts = juce::String();
    if (captureFrame)
    {
        captureFacts = "  |  " + captureTimestamp;
        if (captureVersion.isNotEmpty())
            captureFacts += "  |  v" + captureVersion;
        captureFacts += "  |  ITU-R BS.1770";
    }
    g.drawText (state + juce::String (seconds, 1) + " S" + captureFacts, session,
                juce::Justification::centred);
}

void View::paintLevel (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& meter = observatoryFrame.meter;
    const auto& delta = observatoryFrame.delta;
    const bool currentAvailable = currentFactsAvailable();
    const bool cumulativeAvailable = cumulativeFactsAvailable();
    const auto density = currentPreset().density;
    const auto compact = density == Density::compact;
    juce::Rectangle<int> channelStrips;
    juce::Rectangle<int> clipEventRail;
    if (target() == ObservationTarget::absolute
        && (density == Density::standard || density == Density::observatory))
        channelStrips = area.removeFromRight (
            density == Density::observatory ? 76 : 62).reduced (2);
    else if (target() == ObservationTarget::absolute)
        clipEventRail = area.removeFromBottom (compact ? 17 : 19).reduced (2, 1);
    if (target() == ObservationTarget::delta)
    {
        const std::array<double, 3> values { delta.lufs, delta.lufs_s, delta.true_peak };
        const std::array<const char*, 3> labels { "M", "S", "TP" };
        const auto cellWidth = area.getWidth() / (compact ? 2 : 3);
        for (int index = 0; index < (compact ? 2 : 3); ++index)
            drawMetric (g, area.removeFromLeft (cellWidth).reduced (2),
                        hypha::delta() + labels[(size_t) index],
                        optionValue (values[(size_t) index], deltaFactsAvailable()),
                        index == 2 ? "dB" : "LU", compact ? 27.0f : 36.0f, true);
        return;
    }

    const auto mainHeight = compact ? area.getHeight() : juce::roundToInt (area.getHeight() * 0.58f);
    auto main = area.removeFromTop (mainHeight);
    const std::array<double, 3> mainValues { meter.lufs_m, meter.lufs_s, meter.lufs_i };
    const std::array<const char*, 3> mainLabels { "M", "S", "I" };
    const auto mainCount = compact ? 2 : 3;
    for (int index = 0; index < mainCount; ++index)
        drawMetric (g, main.removeFromLeft (main.getWidth() / (mainCount - index)).reduced (2),
                    mainLabels[(size_t) index],
                    optionValue (mainValues[(size_t) index],
                                 index < 2 ? currentAvailable : cumulativeAvailable), "LUFS",
                    compact ? 28.0f : 42.0f);
    if (compact)
    {
        if (! clipEventRail.isEmpty())
            paintClipEventRail (g, clipEventRail);
        return;
    }

    const std::array<double, 5> supportValues {
        meter.true_peak, meter.max_true_peak, meter.lra, meter.plr, meter.correlation
    };
    const std::array<bool, 5> supportAvailable {
        currentAvailable, cumulativeAvailable,
        cumulativeAvailable && observatoryFrame.lra_state == KIRIN_LRA_READY,
        cumulativeAvailable, currentAvailable
    };
    const std::array<const char*, 5> supportLabels { "TP", "MAX TP", "LRA", "PLR", "CORR" };
    const std::array<const char*, 5> supportUnits { "dBTP", "dBTP", "LU", "dB", "" };
    for (int index = 0; index < 5; ++index)
    {
        const auto warming = index == 2 && cumulativeAvailable
                          && observatoryFrame.lra_state == KIRIN_LRA_WARMING;
        const auto warmingText = warming
            ? "WARM " + juce::String ((int) std::floor (observatoryFrame.lra_elapsed_seconds)) + "S"
            : juce::String();
        drawMetric (g, area.removeFromLeft (area.getWidth() / (5 - index)).reduced (2),
                    supportLabels[(size_t) index],
                    optionValue (supportValues[(size_t) index], supportAvailable[(size_t) index]),
                    supportUnits[(size_t) index], 18.0f, index == 4, index == 4 ? 2 : 1,
                    warmingText);
    }
    if (! channelStrips.isEmpty())
        paintChannelStrips (g, channelStrips);
    else if (! clipEventRail.isEmpty())
        paintClipEventRail (g, clipEventRail);
}

void View::paintTime (juce::Graphics& g, juce::Rectangle<int> area)
{
    time_history::paint (g, area, history, historyRequest().label,
                         target() == ObservationTarget::delta);
}
}
