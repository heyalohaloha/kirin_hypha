#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "DisplaySmoother.h"
#include "HyphaTheme.h"
#include "HyphaWidgets.h"
#include "PostControls.h"

// B-054: full UI rebuild to egui parity (crates/hypha_pre/editor.rs + hypha_post/editor.rs +
// hypha_gui). 300×200 mycelium-textured panel. No measurement logic lives here (R-12 / R-22):
// the editor only formats values the Rust engine produces. palette.rs is the colour source of
// truth (no new colours hardcoded). No red / pure white (#ffffff) / neon (品位原則 / G-72-10).
//
// PRE: title "PRE" + click-to-edit Name (→ kirin_hypha_set_pre_name) + flora line + Watch(current+MAX)/
//      Record(6) metric grid (per-cell hover help) + Keeping banner + 5-state static LED.
// POST: title "POST" + Record pair label + click-to-edit pair name (→ set_pair_target) + flora +
//      display-branch grid (Bypassed/Inactive→"---" ; pair-empty/PRE bypassed→absolute ;
//      paired Stale/NoPre→muted Δ/--- ; PRE Bypassed/Inactive→POST absolute ; Δ Active ; Record→6) +
//      Keep/Stop/Sense hint + one prioritized feedback row + playback pair
//      lock + LED + exact candidate dropdown + All Keep/All Stop. (Proposals cards remain egui-only.)
class KirinHyphaEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit KirinHyphaEditor (KirinHyphaProcessorBase&);
    ~KirinHyphaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class PairMenuLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        PairMenuLookAndFeel()
        {
            setColour (juce::PopupMenu::backgroundColourId, hypha::BG);
            setColour (juce::PopupMenu::textColourId, hypha::COL_NORMAL);
            setColour (juce::PopupMenu::headerTextColourId, hypha::COL_FLORA);
            setColour (juce::PopupMenu::highlightedBackgroundColourId,
                       hypha::kFieldFill.brighter (0.08f));
            setColour (juce::PopupMenu::highlightedTextColourId, hypha::COL_FLORA_BR);
        }
        juce::Font getPopupMenuFont() override
        {
            return hypha::monoFont (hypha::ui_contract::menuFontHeight);
        }
    };

    void timerCallback() override;
    void updatePre();
    void updatePost();

    // Which metric grid is configured (label/unit/font set). Abs* uses absolute labels
    // (LUFS-M/TP/…); Delta* uses Δ labels (ΔLUFS/…). Watch is current|MAX; Record is 2×3.
    enum class Kind { WatchAbs6, WatchDelta6, Abs6, Delta6 };
    void configureForKind (Kind);
    void layoutMetrics (bool six);
    void showCandidateMenu();          // B-102: POST pair dropdown (All Keep/All Stop/candidates)
    void handleCandidateMenu (int result,
                              const juce::Array<KirinHyphaProcessorBase::PreCandidate>& candidates);
    static PairMenuLookAndFeel& pairMenuLookAndFeel();
    void fillAbs (int cell, double v, bool isTp, bool muted = false);
    void fillDelta (int cell, double v, bool isTp, juce::Colour deltaBase, bool tpWarn, bool muted = false);
    void showToast (const juce::String& msg);
    void updateFeedback (double now, bool keeping, const juce::String& persistentError);
    juce::String instanceId8() const; // first 8 chars of instance_id (empty-name fallback)
    double nowSecs() const { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    KirinHyphaProcessorBase& processorRef;
    const bool isPost;

    hypha::MyceliumBackground bg;
    hypha::StatusLed          led;
    hypha::EditableName       nameField;                  // PRE name / POST pair name
    juce::Label               pairStatusLabel;            // PRE/POST: PAIR — / ◌ / ●
    std::array<hypha::MetricCell, 6> cells;
    hypha::LoudnessSelector   loudnessSelector;           // occupies cell 0's existing label column
    juce::Label               feedbackLabel;              // toast > persistent error > Keeping
#if KIRIN_HYPHA_PRE_DISPLAY
    juce::Label               preDisplayPrimaryLabel;      // PRE-only current/next measured fact
    juce::Label               preDisplayDetailLabel;       // PRE-only bounded context line
#endif
    std::unique_ptr<hypha::PostControls> postControls;    // POST button row
    juce::TextButton          pairDropdown;                // POST: ▼ candidate / All Keep / All Stop
    juce::TooltipWindow       tooltip { this };           // drives per-cell hover help

    Kind   currentKind = Kind::WatchAbs6;
    bool   currentSix  = false;
    int    metricTop   = 0;       // y of the first metric row (set in resized())
    int    floraY      = 0;       // y of the flora separator line
    juce::Rectangle<int> titleArea;

    bool   prevAck     = false;
    double bannerUntil = 0.0;
    double toastUntil  = 0.0;
    juce::String toastText;
    double pathAnomalyUntil = 0.0;        // B-128 (G-115-371 D3): restore identity anomaly latch
    juce::String pathAnomalyText;         //   drained 文言を fade まで保持
    hypha::DisplaySmoother displaySmoother;
    KirinMeasureResult watchMaximum {};
    bool haveWatchMaximum = false;
    KirinRecordDisplay cachedRecordDisplay {};
    bool haveRecordDisplay = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KirinHyphaEditor)
};
