#include "PluginEditor.h"

#include <cmath>

namespace
{
    // Biophilic / Dark Cockpit palette. No pure #ffffff (G-72-10), no red (品位原則), no neon.
    const juce::Colour kBg    (0xff141a16); // deep green-black
    const juce::Colour kValue (0xffb4c9ab); // muted sage — active values (COL_NORMAL equiv)
    const juce::Colour kLabel (0xff8a9a82); // dimmer sage — labels
    const juce::Colour kUnit  (0xff66735f); // muted — units
    const juce::Colour kMuted (0xff5f6b59); // muted — "---" / Stale Δ / disabled (COL_MUTED equiv)
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
    const double kNaN = std::numeric_limits<double>::quiet_NaN();

    auto showAbsolute = [this, kNaN]
    {
        showDelta = false;
        deltaMuted = false;
        KirinMeasureResult r;
        if (processorRef.pollMeasureResult (r))
        {
            lufsM = r.lufs_m; truePeak = r.true_peak; crest = r.crest; // NaN sentinel preserved
        }
        else
        {
            lufsM = truePeak = crest = kNaN; // null handle / lock contention -> "---"
        }
    };

    if (! isPost)
    {
        // PRE: always the absolute three values (poll_result NaN on non-Active -> "---").
        showAbsolute();
    }
    else
    {
        // B-073 POST display branching (no freeze — see header):
        //   Bypassed / Inactive               -> "---"           (editor.rs:450-460 equiv)
        //   Active + pair empty               -> POST absolute   (editor.rs:474-475)
        //   Active + pair set + Active/Stale  -> Δ (Stale muted) (editor.rs:477-486)
        //   Active + pair set + NoPre         -> POST absolute   (editor.rs:498-499; last_active cleared)
        const int sig = processorRef.signalState(); // 0=Inactive 1=Active 2=Bypassed
        if (sig != 1)
        {
            showDelta = false;
            deltaMuted = false;
            lufsM = truePeak = crest = kNaN; // Bypassed / Inactive -> "---"
        }
        else
        {
            const bool pairEmpty = processorRef.pairName().isEmpty();
            KirinDelta d;
            const bool haveDelta = processorRef.pollDelta (d);
            // KirinDelta.mode: 0=Active 1=Stale 2=NoPre.
            if (! pairEmpty && haveDelta && (d.mode == 0 || d.mode == 1))
            {
                showDelta = true;
                deltaMuted = (d.mode == 1); // Stale -> muted (COL_MUTED equiv)
                lufsM = d.lufs; truePeak = d.true_peak; crest = d.crest; // NaN sentinel preserved
            }
            else
            {
                showAbsolute(); // pair empty / NoPre / no delta -> POST absolute
            }
        }

        // B-072 pairing controls: Keep on Watch (Os + name); Stop on Record; name locked while recording.
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

    // Δ (U+0394) built from its codepoint to avoid non-ASCII source/escape issues.
    const juce::String dlt = juce::String::charToString ((juce::juce_wchar) 0x0394);
    struct Row { juce::String label; double val; const char* unit; };
    const Row rows[] = {
        { showDelta ? dlt + "LUFS"  : juce::String ("LUFS-M"), lufsM,    "LUFS" },
        { showDelta ? dlt + "TP"    : juce::String ("TP"),     truePeak, "dBTP" },
        { showDelta ? dlt + "Crest" : juce::String ("Crest"),  crest,    "dB"   },
    };

    // Stale Δ shows every value muted; otherwise active values are sage, "---" is muted.
    const bool allMuted = (showDelta && deltaMuted);

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

        g.setColour ((hasVal && ! allMuted) ? kValue : kMuted);
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
