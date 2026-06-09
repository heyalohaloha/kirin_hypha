#pragma once

#include <limits>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

// 段C minimal Watch UI: polls kirin_hypha_poll_result at ~10fps and shows the three
// Watch values (LUFS-M / True Peak / Crest). No measurement logic lives here — it only
// formats values the Rust engine produces (R-12 / R-22). NaN (= no value, or non-Active
// signal state cleared by the measure thread) renders as "---" (parity: editor.rs SS-7).
// Role-agnostic (B-070): used by both PRE and POST; the title row uses processor.getName().
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

    // Latest polled values. NaN -> "---".
    double lufsM    = std::numeric_limits<double>::quiet_NaN();
    double truePeak = std::numeric_limits<double>::quiet_NaN();
    double crest    = std::numeric_limits<double>::quiet_NaN();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KirinHyphaEditor)
};
