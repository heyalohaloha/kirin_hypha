#include "HyphaObservatoryView.h"
#include "HyphaRunSummary.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTimeHistoryPainter.h"
#include "HyphaTextStyle.h"

#include <cmath>

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

void View::layoutFooterActions (juce::Rectangle<int> actions)
{
    const bool reference = selectedDomain == Domain::reference;
    const bool full = captureEntryAvailable (role, currentPreset()) && ! reference;
    hybridVuButton.setVisible (! captureFrame);
    clearPeakClipButton.setVisible (false);
    operationsButton.setVisible (! captureFrame);
    stopButton.setVisible (role == Role::post && keepActive && ! captureFrame);
    resetButton.setVisible (full && ! captureFrame);
    noteButton.setVisible (role == Role::post && full && noteButton.isEnabled() && ! captureFrame);
    captureButton.setVisible (full && ! captureFrame);
    localBlindButton.setVisible (false);
    juce::Array<juce::Button*> visible;
    for (auto* button : { &hybridVuButton, &resetButton, &noteButton, &captureButton,
                          &stopButton, &operationsButton })
        if (button->isVisible()) visible.add (button);
    juce::Array<int> minimumWidths;
    const auto actionFont = labelFont (presentationContext(), typography::TextRole::action);
    int minimumTotal = 0;
    for (auto* button : visible)
    {
        const auto width = juce::roundToInt (
            std::ceil (actionFont.getStringWidthFloat (button->getButtonText()) + 6.0f));
        minimumWidths.add (width);
        minimumTotal += width;
    }
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

void View::paintFooter (juce::Graphics& g, const ShellLayout& layout)
{
    drawPanel (g, toJuce (layout.footer), experienceFamily(), 4.0f);
    auto session = sessionArea.reduced (6, 0);
    const auto& meter = observatoryFrame.meter;
    const auto state = ! frameAvailable || meter.state == KIRIN_METER_SESSION_EMPTY
                         ? juce::String ("WAITING")
                     : observatoryFrame.signal_state == KIRIN_SIGNAL_STATE_BYPASSED
                         ? juce::String ("BYPASSED") : juce::String();
    const auto seconds = frameAvailable && meter.sample_rate > 0
        ? static_cast<double> (meter.active_frames) / static_cast<double> (meter.sample_rate) : 0.0;
    g.setColour (frameAvailable ? COL_TEXT_SECONDARY : COL_MUTED);
    if (! captureFrame)
    {
        g.setFont (monoFont (presentationContext(), typography::TextRole::status));
        if (feedbackText.isEmpty())
            g.drawText (state, session, juce::Justification::centredLeft);
       #if defined(JucePlugin_VersionString)
        const auto version = juce::String ("v") + JucePlugin_VersionString;
       #else
        const auto version = juce::String ("development");
       #endif
        if (getWidth() >= 600 && feedbackText.isEmpty())
            g.drawText (version, session, juce::Justification::centredRight);
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
    if (showRunSummary && target() == ObservationTarget::absolute)
        run_summary::paint (g, area, runSummary,
                            frameAvailable ? observatoryFrame.meter.sample_rate : 0.0,
                            presentationContext());
    else
        time_history::paint (g, area, history, compact ? historyRequest().label : "",
                             target() == ObservationTarget::delta, compact, selectedScaleMode,
                             presentationContext());
}
}
