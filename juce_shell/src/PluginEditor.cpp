#include "PluginEditor.h"

#include <cmath>

namespace
{
    // Biophilic / Dark Cockpit palette. No pure #ffffff (G-72-10), no red (品位原則).
    const juce::Colour kBg    (0xff141a16); // deep green-black
    const juce::Colour kValue (0xffb4c9ab); // muted sage — active values
    const juce::Colour kLabel (0xff8a9a82); // dimmer sage — labels
    const juce::Colour kUnit  (0xff66735f); // muted — units
    const juce::Colour kMuted (0xff5f6b59); // muted — "---"
}

KirinHyphaEditor::KirinHyphaEditor (KirinHyphaProcessorBase& p)
    : juce::AudioProcessorEditor (&p), processorRef (p)
{
    setSize (220, 150);
    startTimerHz (10); // ~10fps, parity with editor.rs:292 (100ms repaint)
}

KirinHyphaEditor::~KirinHyphaEditor()
{
    stopTimer();
}

juce::String KirinHyphaEditor::fmtVal (double v)
{
    if (std::isnan (v))
        return "---";
    return juce::String (v, 1); // 1 decimal place (hypha_gui::fmt_val)
}

void KirinHyphaEditor::timerCallback()
{
    KirinMeasureResult r;
    if (processorRef.pollMeasureResult (r))
    {
        lufsM    = r.lufs_m;     // NaN sentinel preserved -> "---" when no value
        truePeak = r.true_peak;
        crest    = r.crest;
    }
    else
    {
        // poll failed (null handle / lock contention): show "---" (does not crash).
        lufsM = truePeak = crest = std::numeric_limits<double>::quiet_NaN();
    }

    repaint();
}

void KirinHyphaEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    auto area = getLocalBounds().reduced (14);

    g.setColour (kLabel);
    g.setFont (juce::Font (13.0f, juce::Font::bold));
    // Role-aware title: "Kirin Hypha PRE" / "Kirin Hypha POST" from the processor.
    g.drawText (processorRef.getName(), area.removeFromTop (22), juce::Justification::centredLeft);
    area.removeFromTop (8);

    struct Row { const char* label; double val; const char* unit; };
    const Row rows[] = {
        { "LUFS-M", lufsM,    "LUFS" },
        { "TP",     truePeak, "dBTP" },
        { "Crest",  crest,    "dB"   },
    };

    const int rowH = 30;
    for (const auto& r : rows)
    {
        auto row = area.removeFromTop (rowH);

        g.setColour (kLabel);
        g.setFont (juce::Font (13.0f));
        g.drawText (r.label, row.removeFromLeft (66), juce::Justification::centredLeft);

        auto unitArea = row.removeFromRight (50);

        const juce::String text = fmtVal (r.val);
        const bool hasVal = (text != "---");

        g.setColour (hasVal ? kValue : kMuted);
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::plain));
        g.drawText (text, row, juce::Justification::centredRight);

        g.setColour (kUnit);
        g.setFont (juce::Font (11.0f));
        g.drawText (r.unit, unitArea.reduced (4, 0), juce::Justification::centredLeft);
    }
}

void KirinHyphaEditor::resized()
{
    // Fixed layout computed in paint().
}
