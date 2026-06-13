#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "HyphaTheme.h"
#include "HyphaWidgets.h"
#include "PostControls.h"

// B-054: full UI rebuild to egui parity (crates/hypha_pre/editor.rs + hypha_post/editor.rs +
// hypha_gui). 300×200 mycelium-textured panel. No measurement logic lives here (R-12 / R-22):
// the editor only formats values the Rust engine produces. palette.rs is the colour source of
// truth (no new colours hardcoded). No red / pure white (#ffffff) / neon (品位原則 / G-72-10).
//
// PRE: title "PRE" + click-to-edit Name (→ kirin_hypha_set_pre_name) + flora line + Watch(3)/
//      Record(6) metric grid (per-cell hover help) + Keeping banner + 5-state breathing LED.
// POST: title "POST" + Record pair label + click-to-edit pair name (→ set_pair_target) + flora +
//      display-branch grid (Bypassed/Inactive→"---" ; pair-empty→absolute ; Δ Active/Stale ;
//      Record→Δ6) + Keep/Stop/Note→[Good][Fix][Hold][Cancel]/Sense hint + Toast + playback pair
//      lock + LED. (Out of scope A: candidate ComboBox, All Keep/All Stop, proposals cards.)
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
    void updatePre();
    void updatePost();

    // Which metric grid is configured (label/unit/font set). Abs* uses absolute labels
    // (LUFS-M/TP/…); Delta* uses Δ labels (ΔLUFS/…). 3 = Watch/3-up, 6 = Record/2×3.
    enum class Kind { Abs3, Delta3, Abs6, Delta6 };
    void configureForKind (Kind);
    void layoutMetrics (bool six);
    void showCandidateMenu();          // B-102: POST pair dropdown (All Keep/All Stop/candidates)
    void handleCandidateMenu (int result);
    void fillAbs (int cell, double v, bool isTp);
    void fillDelta (int cell, double v, bool isTp, juce::Colour deltaBase, bool tpWarn);
    void showToast (const juce::String& msg);
    juce::String instanceId8() const; // first 8 chars of instance_id (empty-name fallback)
    double nowSecs() const { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    KirinHyphaProcessorBase& processorRef;
    const bool isPost;

    hypha::MyceliumBackground bg;
    hypha::StatusLed          led;
    hypha::EditableName       nameField;                  // PRE name / POST pair name
    juce::Label               pairRecLabel;               // POST: "pair: …" shown during Record
    std::array<hypha::MetricCell, 6> cells;
    juce::Label               bannerLabel;                // "Keeping" (ACK edge, 3s) — PRE
    juce::Label               toastLabel;                 // POST transient messages (3s)
    juce::Label               recordErrorLabel;           // B-118 (③/追補): PRE+POST io-fail status (persistent / G-115-29)
    std::unique_ptr<hypha::PostControls> postControls;    // POST button row
    juce::TextButton          pairDropdown;                // POST: ▼ candidate / All Keep / All Stop
    juce::StringArray         menuCandidateNames;          // maps PopupMenu result -> candidate name
    juce::TooltipWindow       tooltip { this };           // drives per-cell hover help

    Kind   currentKind = Kind::Abs3;
    bool   currentSix  = false;
    int    metricTop   = 0;       // y of the first metric row (set in resized())
    int    floraY      = 0;       // y of the flora separator line
    juce::Rectangle<int> titleArea;

    bool   prevAck     = false;
    double bannerUntil = 0.0;
    double toastUntil  = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KirinHyphaEditor)
};
