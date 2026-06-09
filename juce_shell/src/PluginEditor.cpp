#include "PluginEditor.h"

#include <cmath>

namespace
{
    // Biophilic / Dark Cockpit palette. No pure #ffffff (G-72-10), no red (品位原則), no neon.
    const juce::Colour kBg    (0xff141a16); // deep green-black
    const juce::Colour kValue (0xffb4c9ab); // muted sage — active values
    const juce::Colour kLabel (0xff8a9a82); // dimmer sage — labels
    const juce::Colour kUnit  (0xff66735f); // muted — units
    const juce::Colour kMuted (0xff5f6b59); // muted — "---" / disabled
    const juce::Colour kField (0xff1c241e); // slightly lifted green-black — input/button fill
    const juce::Colour kEdge  (0xff3a463c); // muted edge — outlines

    // Allowed pair-name characters: ASCII graphic + space (matches kirin_measure::sanitize_name).
    juce::String allowedNameChars()
    {
        juce::String s;
        for (int c = 0x20; c <= 0x7E; ++c)
            s += (juce::juce_wchar) c;
        return s;
    }
}

KirinHyphaEditor::KirinHyphaEditor (KirinHyphaProcessorBase& p)
    : juce::AudioProcessorEditor (&p), processorRef (p), isPost (p.isPostRole())
{
    setSize (220, isPost ? 250 : 150);

    if (isPost)
    {
        pairLabel.setText ("Pair PRE", juce::dontSendNotification);
        pairLabel.setColour (juce::Label::textColourId, kLabel);
        pairLabel.setFont (juce::Font (12.0f));
        addAndMakeVisible (pairLabel);

        nameEditor.setText (processorRef.pairName(), juce::dontSendNotification);
        nameEditor.setInputRestrictions (16, allowedNameChars()); // parity with sanitize_name
        nameEditor.setTextToShowWhenEmpty ("name", kMuted);
        nameEditor.setColour (juce::TextEditor::backgroundColourId, kField);
        nameEditor.setColour (juce::TextEditor::textColourId, kValue);
        nameEditor.setColour (juce::TextEditor::outlineColourId, kEdge);
        nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, kLabel);
        nameEditor.setColour (juce::CaretComponent::caretColourId, kLabel);
        nameEditor.onReturnKey = [this] { processorRef.setPairName (nameEditor.getText()); };
        nameEditor.onFocusLost = [this] { processorRef.setPairName (nameEditor.getText()); };
        addAndMakeVisible (nameEditor);

        for (auto* b : { &keepButton, &stopButton })
        {
            b->setColour (juce::TextButton::buttonColourId, kField);
            b->setColour (juce::TextButton::textColourOnId, kValue);
            b->setColour (juce::TextButton::textColourOffId, kValue);
            addAndMakeVisible (b);
        }
        keepButton.onClick = [this] { processorRef.keepPair(); };
        stopButton.onClick = [this] { processorRef.stopPair(); };
    }

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

    if (isPost)
    {
        // Keep on Watch (Os + a pair name set); Stop on Record. Pairing is locked while
        // recording. The FFI re-validates (keep is a no-op if not Os / not unique PRE).
        const bool rec     = processorRef.isRecording();
        const bool os      = (processorRef.licenseCode() == 0);
        const bool hasName = nameEditor.getText().isNotEmpty();
        keepButton.setVisible (! rec);
        keepButton.setEnabled (os && hasName);
        stopButton.setVisible (rec);
        nameEditor.setEnabled (! rec);
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
    if (! isPost)
        return; // PRE: fixed layout computed in paint().

    // POST pairing row sits below the three value rows. Inner area starts at (14,14);
    // title(22)+gap(8)+3*30 = 120, so values end ~134; lay the controls below that.
    auto area = getLocalBounds().reduced (14);
    area.removeFromTop (22 + 8 + 3 * 30 + 10); // skip title + values + a small gap

    auto labelRow = area.removeFromTop (16);
    pairLabel.setBounds (labelRow);
    area.removeFromTop (2);

    nameEditor.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);

    auto buttonRow = area.removeFromTop (26);
    // Keep and Stop occupy the same slot (toggled by visibility); give each the full row.
    keepButton.setBounds (buttonRow);
    stopButton.setBounds (buttonRow);
}
