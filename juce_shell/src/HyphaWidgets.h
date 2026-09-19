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
        std::function<void()> onPreviewDemand;
        std::function<void()> onSelect; // when present, click selects instead of editing

        void setPrefix (const juce::String& p)        { prefix = p; if (! editing) repaint(); }
        void setFallback (const juce::String& f)       { fallback = f; if (! editing) repaint(); }
        bool setSelectionPreview (const juce::String&, std::uint64_t generation);
        std::uint64_t paintedSelectionGeneration() const noexcept { return paintedPreview; }
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
        void mouseEnter (const juce::MouseEvent&) override { if (onPreviewDemand) onPreviewDemand(); }
        void focusGained (FocusChangeType) override { if (onPreviewDemand) onPreviewDemand(); }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (onSelect && (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey))
            { onSelect(); return true; }
            return false;
        }
        void resized() override;

    private:
        void startEditing();
        void commitEditing();
        void cancelEditing();

        juce::String rawName, prefix, fallback, enabledTooltip, lockedTooltip, selectionPreview;
        std::uint64_t previewGeneration = 0, paintedPreview = 0;
        bool editing = false;
        bool editingEnabled = true;
        std::unique_ptr<juce::TextEditor> editor;
        presentation::Context presentationContext = presentation::defaultContext();
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditableName)
    };
}
