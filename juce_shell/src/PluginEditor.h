#pragma once

#include <limits>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

// 段C minimal Watch UI: polls kirin_hypha_poll_result at ~10fps and shows the three
// Watch values (LUFS-M / True Peak / Crest). No measurement logic lives here — it only
// formats values the Rust engine produces (R-12 / R-22). NaN (= no value, or non-Active
// signal state cleared by the measure thread) renders as "---" (parity: editor.rs SS-7).
// Role-agnostic (B-070): the title row uses processor.getName().
//
// B-072: for the POST role the editor also shows a pairing row — a sanitized pair-PRE-name
// field plus Keep (Watch, Os only, name set) / Stop (Record) buttons calling the FFI keep /
// stop. The PRE role shows no pairing controls. No Δ display in this step. No red / pure
// white / neon (品位原則 / G-72-10) — Biophilic / Dark Cockpit palette only.
class KirinHyphaEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit KirinHyphaEditor (KirinHyphaProcessorBase&);
    ~KirinHyphaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    static juce::String fmtVal (double v); // parity with hypha_gui fmt_val: {:.1} / "---"

    KirinHyphaProcessorBase& processorRef;
    const bool isPost;                     // pairing controls shown only for POST

    // POST pairing controls (constructed always; only added/wired when isPost).
    juce::Label      pairLabel;
    juce::TextEditor nameEditor;
    juce::TextButton keepButton { "Keep" };
    juce::TextButton stopButton { "Stop" };

    // Latest polled values. NaN -> "---".
    double lufsM    = std::numeric_limits<double>::quiet_NaN();
    double truePeak = std::numeric_limits<double>::quiet_NaN();
    double crest    = std::numeric_limits<double>::quiet_NaN();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KirinHyphaEditor)
};
