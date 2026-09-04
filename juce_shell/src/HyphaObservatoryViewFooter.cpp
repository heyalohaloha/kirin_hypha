#include "HyphaObservatoryView.h"
#include "HyphaRunSummary.h"
#include "HyphaTimeHistoryPainter.h"

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
    g.setColour (BG.withAlpha (opacity));
    g.fillRoundedRectangle (area.toFloat(), corner);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), corner, 1.0f);
}
}

void View::setNoteAvailability (bool osOwned, bool recording)
{
    const auto help = ! osOwned ? juce::String ("NOTE requires Kirin OS")
                    : ! recording ? juce::String ("NOTE requires an active Keep")
                                  : juce::String ("Add a note at the current sample position");
    noteButton.setEnabled (osOwned && recording);
    noteButton.setTitle (help);
    noteButton.setDescription (help);
    noteButton.setTooltip (help);
}

void View::layoutFooterActions (juce::Rectangle<int> actions)
{
    const bool reference = selectedDomain == Domain::reference;
    const bool full = captureEntryAvailable (role, currentPreset()) && ! reference;
    resetButton.setVisible (! captureFrame && ! reference);
    noteButton.setVisible (role == Role::post && ! captureFrame && ! reference);
    captureButton.setVisible (full && ! captureFrame);
    juce::Array<juce::Button*> visible;
    for (auto* button : { &resetButton, &noteButton, &captureButton })
        if (button->isVisible()) visible.add (button);
    const int width = visible.isEmpty() ? 0 : actions.getWidth() / visible.size();
    for (int index = 0; index < visible.size(); ++index)
        visible[index]->setBounds ((index + 1 == visible.size()
            ? actions : actions.removeFromLeft (width)).reduced (1, 2));
}

void View::paintFooter (juce::Graphics& g, const ShellLayout& layout)
{
    drawPanel (g, toJuce (layout.footer), experienceFamily(), 4.0f);
    auto session = sessionArea.reduced (6, 0);
    const auto& meter = observatoryFrame.meter;
    const auto state = ! frameAvailable ? juce::String ("SESSION ") + hypha::emDash()
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
        const auto density = currentPreset().density;
        g.setFont (monoFont (density == Density::compact ? 8.5f
                           : density == Density::inspection ? 14.0f : 10.5f));
       #if defined(JucePlugin_VersionString)
        const auto version = juce::String ("  |  v") + JucePlugin_VersionString;
       #else
        const auto version = juce::String ("  |  development");
       #endif
        g.drawFittedText (state + juce::String (seconds, 1) + " S" + version, session,
                          juce::Justification::centred, 1, 0.80f);
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
    area.removeFromTop (timeNavigationHeight (currentPreset().density));
    if (showRunSummary && target() == ObservationTarget::absolute)
        run_summary::paint (g, area, runSummary,
                            frameAvailable ? observatoryFrame.meter.sample_rate : 0.0);
    else
        time_history::paint (g, area, history, compact ? historyRequest().label : "",
                             target() == ObservationTarget::delta, compact, selectedScaleMode);
}
}
