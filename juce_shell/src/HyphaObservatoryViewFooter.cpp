#include "HyphaObservatoryView.h"
#include "HyphaComparisonPresentation.h"
#include "HyphaRunSummary.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTimeHistoryPainter.h"
#include "HyphaTextStyle.h"

#include <cmath>
#include <utility>

namespace hypha::observatory
{
namespace
{
juce::Rectangle<int> toJuce (Rect value)
{
    return { value.x, value.y, value.width, value.height };
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> area,
                ExperienceFamily family, float corner)
{
    const auto opacity = family == ExperienceFamily::compactMeter ? 0.96f : 0.76f;
    surface_material::paintPanel (g, area.toFloat(), opacity, corner);
}
}

void View::setNoteAvailability (bool osOwned, bool recording)
{
    const auto help = ! osOwned ? juce::String ("NOTE requires Kirin OS")
                    : ! recording ? juce::String ("NOTE requires an active Keep")
                                  : juce::String ("Add a note at the current sample position");
    const bool enabled = osOwned && recording;
    const bool visibilityMayChange = noteButton.isEnabled() != enabled;
    noteButton.setEnabled (enabled);
    noteButton.setTitle (help);
    noteButton.setDescription (help);
    noteButton.setTooltip (help);
    if (visibilityMayChange)
        resized();
}

void View::setKeepActive (bool active)
{
    if (keepActive == active) return;
    keepActive = active;
    resized();
}

void View::setChainReadout (juce::String text, bool caution)
{
    if (chainReadoutText == text && chainReadoutCaution == caution) return;
    chainReadoutText = std::move (text);
    chainReadoutCaution = caution;
    repaint (sessionArea);
}

void View::layoutFooterActions (juce::Rectangle<int> actions)
{
    const bool reference = selectedDomain == Domain::reference;
    const bool full = captureEntryAvailable (role, currentPreset()) && ! reference
                   && ! measurementOnlySurround;
    hybridVuButton.setVisible (! captureFrame && ! measurementOnlySurround);
    clearPeakClipButton.setVisible (false);
    operationsButton.setVisible (! captureFrame);
    stopButton.setVisible (role == Role::post && keepActive && ! captureFrame
                           && ! measurementOnlySurround);
    resetButton.setVisible (false); // Reset Meter Session has one entry in MENU.
    noteButton.setVisible (role == Role::post && full && noteButton.isEnabled() && ! captureFrame);
    captureButton.setVisible (false); // Image Capture has one entry in MENU.
    localBlindButton.setVisible (full && localBlindEntryEnabled && ! captureFrame);
    localBlindButton.setEnabled (! keepActive);
    localBlindButton.setButtonText ("BLIND");
    localBlindButton.setTooltip (keepActive ? "Finish Keep / Record before PRE / POST Blind"
        : "Match levels and compare while the song plays");
    liveCompareButton.setVisible (full && liveCompareState.entryEnabled && ! captureFrame);
    liveCompareButton.setButtonText (getWidth() < 900 ? "LISTEN" : "PRE/POST LISTEN");
    if (layoutLiveCompareFooter (actions))
        return;
    // Reserve NOTE and Stop slots: starting a recording does not move primary actions.
    juce::Array<juce::Button*> visible;
    if (full && ! captureFrame)
    {
        visible = { &hybridVuButton, &stopButton, &noteButton };
        if (liveCompareButton.isVisible()) visible.add (&liveCompareButton);
        if (localBlindButton.isVisible()) visible.add (&localBlindButton);
        visible.add (&operationsButton);
    }
    else
        for (auto* button : { &hybridVuButton, &stopButton, &operationsButton })
            if (button->isVisible()) visible.add (button);
    juce::Array<int> widths;
    for (auto* button : visible)
        widths.add (footerButtonWidth (button->getButtonText()));
    placeFooterButtons (visible, widths, actions);
}

int View::footerButtonWidth (const juce::String& text) const
{
    const auto actionFont = labelFont (presentationContext(), typography::TextRole::action);
    return juce::roundToInt (std::ceil (text_style::shownWidth (actionFont, text) + 12.0f));
}

// Each button gets its minimum width, and the rest of the rail is shared out evenly.
void View::placeFooterButtons (const juce::Array<juce::Button*>& visible,
                               const juce::Array<int>& minimumWidths, juce::Rectangle<int> actions)
{
    int minimumTotal = 0;
    for (const auto width : minimumWidths)
        minimumTotal += width;
    auto flexible = juce::jmax (0, actions.getWidth() - minimumTotal);
    for (int index = 0; index < visible.size(); ++index)
    {
        const auto remaining = visible.size() - index;
        const auto width = minimumWidths[index] + flexible / remaining;
        flexible -= flexible / remaining;
        visible[index]->setBounds ((index + 1 == visible.size()
            ? actions : actions.removeFromLeft (width)).reduced (1, 2));
    }
}

int View::statusStripHeight() const
{
    return juce::roundToInt (monoFont (presentationContext(), typography::TextRole::status).getHeight()) + 5;
}

juce::String View::footerStatusText() const
{
    if (measurementFormatHeld) return "FORMAT HELD / STOP KEEP";
    if (! frameAvailable || observatoryFrame.meter.state == KIRIN_METER_SESSION_EMPTY)
        return "WAITING";
    if (observatoryFrame.signal_state == KIRIN_SIGNAL_STATE_BYPASSED)
        return "BYPASSED";
    if (! currentFactsAvailable()) return "HOLD";
    return measurementOnlySurround ? juce::String ("5.1 MEASURE") : juce::String ("LIVE");
}

void View::paintFooter (juce::Graphics& g, const ShellLayout& layout)
{
    drawPanel (g, toJuce (layout.footer), experienceFamily(), 4.0f);
    auto session = sessionArea.reduced (6, 0);
    const auto& meter = observatoryFrame.meter;
    const auto state = footerStatusText();
    const auto seconds = frameAvailable && meter.sample_rate > 0
        ? static_cast<double> (meter.active_frames) / static_cast<double> (meter.sample_rate) : 0.0;
    g.setColour (frameAvailable ? COL_TEXT_SECONDARY : COL_MUTED);
    chainReadoutShown = false;
    if (! captureFrame)
    {
        g.setFont (monoFont (presentationContext(), typography::TextRole::status));
        // Version/build identity belongs to the information menu. It must not compete with
        // live state in this narrow rail (the old development label rendered as "d...").
        const auto font = g.getCurrentFont();
        auto label = state;
        if (text_style::shownWidth (font, label) > (float) session.getWidth())
            label = state == "BYPASSED" ? "BYP" : state == "WAITING" ? "WAIT"
                  : state == "5.1 MEASURE" ? "5.1" : measurementFormatHeld ? "FORMAT" : state;
        // The chain timing the user asked to keep in view goes right of the state. Where the two do
        // not fit, it takes the place of LIVE, HOLD or WAITING, shortened if it must be, while the
        // states that call for action keep the rail. Feedback owns the rail while it lasts, and
        // the folded sizes show the readout in the editor's strip instead.
        const auto width = (float) session.getWidth();
        const auto labelWidth = text_style::shownWidth (font, label);
        auto chain = chainReadoutText; // then without spaces, then "LOAD 31%/70%"
        for (const auto& shorter : { chainReadoutText.replace (" / ", "/"),
                                     chainReadoutText.replace (" / ", "/").fromFirstOccurrenceOf ("CHAIN ", false, false) })
            if (text_style::shownWidth (font, chain) > width) chain = shorter;
        const bool chainWanted = feedbackText.isEmpty() && chainReadoutText.isNotEmpty();
        const bool both = chainWanted && labelWidth + 12.0f + text_style::shownWidth (font, chainReadoutText) <= width;
        const bool replaces = chainWanted && ! both && ! measurementFormatHeld
            && (state == "LIVE" || state == "HOLD" || state == "WAITING")
            && ! chainReadoutText.endsWith ("--") && text_style::shownWidth (font, chain) <= width;
        if (feedbackText.isEmpty() && ! replaces && labelWidth <= width)
            text_style::drawText (g, label, session, juce::Justification::centredLeft, false);
        if (both || replaces)
        {
            g.setColour (chainReadoutCaution ? COL_FLORA_BR : COL_TEXT_SECONDARY);
            text_style::drawText (g, both ? chainReadoutText : chain, session,
                                  both ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
            chainReadoutShown = true;
        }
        return;
    }

    auto upper = session.removeFromTop (session.getHeight() / 2);
    auto lower = session;
    auto provenance = captureTimestamp;
    if (captureVersion.isNotEmpty())
        provenance += "  |  v" + captureVersion;
    g.setFont (monoFont (presentationContext(), typography::TextRole::captureMetadata));
    auto statusArea = upper.removeFromLeft (juce::roundToInt (upper.getWidth() * 0.55f));
    text_style::draw (g, state + juce::String (seconds, 1) + " S  |  ITU-R BS.1770",
                      statusArea, presentationContext(), typography::TextRole::captureMetadata,
                      juce::Justification::centredLeft);
    text_style::draw (g, provenance, upper, presentationContext(),
                      typography::TextRole::captureMetadata,
                      juce::Justification::centredRight);
    const auto metadata = captureMetadata.footerLine();
    if (metadata.isNotEmpty())
    {
        g.setColour (COL_FLORA.withAlpha (0.82f));
        g.setFont (monoFont (presentationContext(), typography::TextRole::captureMetadata));
        text_style::draw (g, metadata, lower, presentationContext(),
                          typography::TextRole::captureMetadata,
                          juce::Justification::centredLeft);
    }
}

void View::paintTime (juce::Graphics& g, juce::Rectangle<int> area)
{
    const bool compact = experienceFamily() == ExperienceFamily::compactMeter;
    area.removeFromTop (timeControlsHeight());
    area.reduce (main_frame::inset(), main_frame::inset()); // the page's main window, in its frame
    if (showRunSummary && target() == ObservationTarget::absolute)
        run_summary::paint (g, area, runSummary,
                            frameAvailable ? observatoryFrame.meter.sample_rate : 0.0,
                            presentationContext(),
                            frameAvailable ? &observatoryFrame.meter : nullptr, true);
    else
        time_history::paint (g, area, history, compact ? historyRequest().label : "",
                             target() == ObservationTarget::delta, compact, selectedScaleMode,
                             presentationContext(),
                             target() == ObservationTarget::delta && frameAvailable
                                 ? comparison_presentation::statusText (
                                       observatoryFrame.comparison_state,
                                       observatoryFrame.comparison_reason)
                                 : juce::String(),
                             currentPreset().density != Density::compact, true);
}
}
