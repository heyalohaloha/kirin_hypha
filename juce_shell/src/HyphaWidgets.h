#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTheme.h"

// B-054: shared GUI primitives ported element-for-element from crates/hypha_gui
// (background.rs / led.rs / common.rs) and the egui PRE/POST name fields. No measurement
// logic lives here (R-12 / R-22): widgets only render values the Rust engine produces.
namespace hypha
{
    // ── led.rs: 5-state static LED ─────────────────────────────────────────────────────────
    enum class LedState
    {
        Idle,            // grey, static
        Error,           // yellow, static (measure thread stopped)
        RecordStandby,   // green dimmed 0.45, static (recording, not yet acked)
        WatchBreathing,  // blue (legacy enum name; rendering is static)
        RecordActive,    // green
        PresetAvailable  // amber
    };

    // derive_led_state priority (first match wins): Error > RecordActive > RecordStandby >
    // PresetAvailable > WatchBreathing > Idle. signalState: 1 = Active (C ABI code).
    LedState deriveLedState (bool measureAlive, int signalState,
                             bool recording, bool recordAcknowledged, bool presetAvailable);

    // Drawn as a 12×12 box with a 5px-radius filled circle (led.rs draw). Colour from the
    // Current state only. setState repaints on transitions; no animation clock is required.
    class StatusLed : public juce::Component,
                      public juce::SettableTooltipClient
    {
    public:
        StatusLed();
        void setState (LedState s); // repaints only on change (no-op otherwise)
        void paint (juce::Graphics&) override;
    private:
        LedState state = LedState::Idle;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusLed)
    };

    // TextButton-compatible POST menu trigger whose arrow is geometry, not a font glyph. This
    // keeps the existing click/menu/accessibility path while avoiding Windows font fallback.
    class PairDropdownButton final : public juce::TextButton
    {
    public:
        PairDropdownButton();
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PairDropdownButton)
    };

    // ── background.rs: mycelium PNG (300×200), drawn OPAQUE over the BG fill. ───────────────
    // The "15%" is the asset's baked-in brightness (RGB <= 96), not an opacity multiplier —
    // egui draws it with Color32::WHITE (background.rs:84). A draw-helper (not a Component) so
    // the editor can paint it first and then its own title/flora chrome on top.
    class MyceliumBackground
    {
    public:
        MyceliumBackground(); // decodes once from embedded BinaryData (invalid -> BG fill only)
        void draw (juce::Graphics&, juce::Rectangle<int> area) const;
    private:
        juce::Image image;
    };

    // ── one metric: [label | value | unit] with a per-cell hover help (egui .on_hover_text). ─
    // Reused for Watch (3 stacked wide cells) and Record (6 cells, 2 columns). label/value/unit
    // font sizes differ between the two modes, so they are set explicitly via setFonts().
    class MetricCell : public juce::Component,
                       public juce::SettableTooltipClient
    {
    public:
        MetricCell();
        void configure (const juce::String& label, const juce::String& unit,
                        const juce::String& help, float minColW);
        void setPresentationContext (presentation::Context next)
        {
            presentationContext = next;
            repaint();
        }
        void setValue (const juce::String& value, juce::Colour valueColour);
        void paint (juce::Graphics&) override;
    private:
        juce::String label, unit, value;
        juce::Colour valueColour = COL_MUTED;
        float minColW = ui_contract::metricMinimumLabelWidth;
        presentation::Context presentationContext = presentation::defaultContext();
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetricCell)
    };

    // Display-only M/S selector occupying the existing first metric label column. The two
    // accessible buttons never alter either measurement engine; they only choose which already
    // computed loudness value the six fixed cells render.
    class LoudnessSelector : public juce::Component
    {
    public:
        LoudnessSelector();

        std::function<void (bool shortTerm)> onChange;

        void setShortTerm (bool shortTerm);
        void setDeltaMode (bool delta);
        void setPresentationContext (presentation::Context next)
        {
            presentationContext = next;
            momentary.setPresentationContext (next);
            shortTerm.setPresentationContext (next);
            resized();
            repaint();
        }
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class SegmentButton final : public juce::Button
        {
        public:
            explicit SegmentButton (const juce::String& text);
            void setSelected (bool selectedIn);
            void setPresentationContext (presentation::Context next) noexcept
            {
                presentationContext = next;
                repaint();
            }
            void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        private:
            const juce::String text;
            bool selected = false;
            presentation::Context presentationContext = presentation::defaultContext();
        };

        SegmentButton momentary { "M" };
        SegmentButton shortTerm { "S" };
        ui_contract::LoudnessSelectorLayout currentLayout() const;
        bool selectedShortTerm = false;
        bool deltaMode = false;
        presentation::Context presentationContext = presentation::defaultContext();
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessSelector)
    };

    // ── editable PRE name / read-only POST exact-pair selector ──────────────────────────────
    // Shows display text (optional prefix + raw name, or a fallback when the name is empty) in
    // COL_FLORA monospace. With onSelect, a click opens the exact selector; otherwise it opens
    // an inline editor seeded with the RAW name. Enter commits (onCommit, sanitized to ≤16 chars
    // by the FFI); Escape / focus loss discards. Editing can be locked (POST playback).
    class EditableName : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        EditableName();

        std::function<void (const juce::String&)> onCommit; // called with the new raw name
        std::function<void()> onSelect; // when present, click selects instead of editing

        void setPrefix (const juce::String& p)        { prefix = p; if (! editing) repaint(); }
        void setFallback (const juce::String& f)       { fallback = f; if (! editing) repaint(); }
        void setModelName (const juce::String& raw);   // edited value; repaints when not editing
                                                        // (named to avoid hiding juce::Component::setName)
        void setEditingEnabled (bool enabled);         // false -> click does nothing (locked)
        void setPresentationContext (presentation::Context next)
        {
            presentationContext = next;
            editor->setFont (monoFont (next, typography::TextRole::selector));
            repaint();
        }
        void setLockedTooltip (const juce::String& t)  { lockedTooltip = t; }
        void setEnabledTooltip (const juce::String& t)
        {
            enabledTooltip = t;
            if (editingEnabled) setTooltip (enabledTooltip);
        }
        bool isEditing() const                         { return editing; }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void resized() override;

    private:
        void startEditing();
        void commitEditing();
        void cancelEditing();

        juce::String rawName, prefix, fallback, enabledTooltip, lockedTooltip;
        bool editing = false;
        bool editingEnabled = true;
        std::unique_ptr<juce::TextEditor> editor;
        presentation::Context presentationContext = presentation::defaultContext();
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditableName)
    };
}
