#pragma once

#include <limits>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

// 段C minimal Watch UI: polls kirin_hypha_poll_result at ~10fps and shows three values.
// No measurement logic lives here — it only formats values the Rust engine produces (R-12 /
// R-22). NaN (= no value, or non-Active signal state cleared by the measure thread) renders
// as "---" (parity: editor.rs SS-7). Role-agnostic (B-070): the title row uses getName().
//
// B-072: POST also shows a pairing row (name field + Keep/Stop, Os-gated). PRE shows none.
// B-073: POST shows Δ instead of the absolute three values when paired and Active with a
// fresh delta (DeltaMode Active/Stale); otherwise the POST absolute three values or "---".
// No freeze (B-048/B-049): kirin_measure clears last_active in exactly the inactive / NoPre
// states, so the editor never has a frozen value to show — it falls back to absolute / "---".
// No red / pure white / neon (品位原則 / G-72-10) — Biophilic / Dark Cockpit palette only.
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
    const bool isPost;                     // pairing controls + Δ shown only for POST

    // POST pairing controls (constructed always; only added/wired when isPost).
    juce::Label      pairLabel;
    juce::TextEditor nameEditor;
    juce::TextButton keepButton { "Keep" };
    juce::TextButton stopButton { "Stop" };

    // Latest displayed values (absolute LUFS-M/TP/Crest, or Δ when showDelta). NaN -> "---".
    double lufsM    = std::numeric_limits<double>::quiet_NaN();
    double truePeak = std::numeric_limits<double>::quiet_NaN();
    double crest    = std::numeric_limits<double>::quiet_NaN();

    bool showDelta  = false;               // B-073: the three values are Δ (ΔLUFS/ΔTP/ΔCrest)
    bool deltaMuted = false;               // B-073: Stale Δ -> muted colour

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KirinHyphaEditor)
};
