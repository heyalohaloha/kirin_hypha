#include "HyphaWidgets.h"

#include "BinaryData.h"

namespace hypha
{
    namespace
    {
        // Allowed name characters: ASCII graphic + space (parity with the FFI's sanitize_name
        // contract / the prior POST field). The FFI sanitizes its own copy regardless.
        juce::String allowedNameChars()
        {
            juce::String s;
            for (int c = 0x20; c <= 0x7E; ++c)
                s += (juce::juce_wchar) c;
            return s;
        }
    }

    // ── deriveLedState (led.rs priority hierarchy) ─────────────────────────────────────────
    LedState deriveLedState (bool measureAlive, int signalState,
                             bool recording, bool recordAcknowledged, bool presetAvailable)
    {
        const bool active = (signalState == 1); // C ABI: 1 = Active
        if (! measureAlive)
            return LedState::Error;
        if (recording) // recording is a complete block (led.rs:49-60) — never falls through
        {
            if (! recordAcknowledged) return LedState::RecordStandby;        // waiting for PRE ack
            if (active)               return LedState::RecordActive;         // ack + Active -> pulse
            return LedState::RecordStandby;                                  // ack but signal stopped
        }
        if (presetAvailable && active) return LedState::PresetAvailable;
        if (active)                    return LedState::WatchBreathing;
        return LedState::Idle;
    }

    // ── StatusLed ──────────────────────────────────────────────────────────────────────────
    StatusLed::StatusLed()
    {
        setInterceptsMouseClicks (false, false); // purely indicative; never eats clicks
    }

    void StatusLed::setState (LedState s)
    {
        if (s != state)
        {
            state = s;
            repaint();
        }
    }

    void StatusLed::paint (juce::Graphics& g)
    {
        juce::Colour c;
        switch (state)
        {
            case LedState::Idle:            c = COL_LED_GREY; break;
            case LedState::Error:           c = COL_LED_YELLOW; break;
            case LedState::RecordStandby:   c = dim (COL_LED_GREEN, 0.45f); break;
            case LedState::WatchBreathing:  c = COL_LED_BLUE; break;
            case LedState::RecordActive:    c = COL_LED_GREEN; break;
            case LedState::PresetAvailable: c = COL_FLORA; break;
        }

        g.setColour (c);
        const auto b = getLocalBounds().toFloat();
        g.fillEllipse (b.getCentreX() - 5.0f, b.getCentreY() - 5.0f, 10.0f, 10.0f); // radius 5 (led.rs)
    }

    // ── MyceliumBackground ───────────────────────────────────────────────────────────────
    MyceliumBackground::MyceliumBackground()
    {
        image = juce::ImageFileFormat::loadFrom (BinaryData::bg_mycelium_png,
                                                 (size_t) BinaryData::bg_mycelium_pngSize);
    }

    void MyceliumBackground::draw (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        g.setColour (BG);
        g.fillRect (area); // fallback / behind a non-opaque image
        if (image.isValid())
        {
            // Opaque, stretched to fill (300×200 asset == window, so 1:1). LINEAR by default.
            g.drawImage (image, area.toFloat(), juce::RectanglePlacement::stretchToFit);
        }
    }

    // ── MetricCell ─────────────────────────────────────────────────────────────────────────
    MetricCell::MetricCell()
    {
        setInterceptsMouseClicks (true, false); // hover -> tooltip (SettableTooltipClient)
    }

    void MetricCell::configure (const juce::String& labelIn, const juce::String& unitIn,
                                const juce::String& help, float labelSizeIn, float valueSizeIn,
                                float unitSizeIn, float minColWIn)
    {
        label = labelIn; unit = unitIn;
        labelSize = labelSizeIn; valueSize = valueSizeIn; unitSize = unitSizeIn; minColW = minColWIn;
        setTooltip (help); // same help on the whole cell (egui attaches it to label/value/unit)
    }

    void MetricCell::setValue (const juce::String& v, juce::Colour vc)
    {
        if (v != value || vc != valueColour)
        {
            value = v; valueColour = vc;
            repaint();
        }
    }

    void MetricCell::paint (juce::Graphics& g)
    {
        // Left-packed columns (parity with egui Grid: min_col_width floor, content auto-expands,
        // 6px horizontal spacing, all left-aligned). label | value | unit.
        const float spacing = 6.0f;
        const auto  labelF = labelFont (labelSize);
        const auto  valueF = monoFont  (valueSize);
        const auto  unitF  = labelFont (unitSize);

        const float labelW = juce::jmax (minColW, labelF.getStringWidthFloat (label));
        const float valueW = juce::jmax (minColW, valueF.getStringWidthFloat (value));

        const float h = (float) getHeight();
        float x = 0.0f;

        g.setFont (labelF);
        g.setColour (COL_MUTED);
        g.drawText (label, juce::Rectangle<float> (x, 0.0f, labelW, h), juce::Justification::centredLeft);
        x += labelW + spacing;

        g.setFont (valueF);
        g.setColour (valueColour);
        g.drawText (value, juce::Rectangle<float> (x, 0.0f, valueW, h), juce::Justification::centredLeft);
        x += valueW + spacing;

        g.setFont (unitF);
        g.setColour (COL_MUTED);
        g.drawText (unit, juce::Rectangle<float> (x, 0.0f, juce::jmax (0.0f, (float) getWidth() - x), h),
                    juce::Justification::centredLeft);
    }

    // ── EditableName ─────────────────────────────────────────────────────────────────────
    EditableName::EditableName()
    {
        setWantsKeyboardFocus (true);

        editor = std::make_unique<juce::TextEditor>();
        editor->setWantsKeyboardFocus (true);
        editor->setMultiLine (false);
        editor->setReturnKeyStartsNewLine (false);
        editor->setInputRestrictions (16, allowedNameChars()); // parity with sanitize_name (≤16)
        editor->setFont (monoFont (14.0f));
        editor->setColour (juce::TextEditor::backgroundColourId, kFieldFill);
        editor->setColour (juce::TextEditor::textColourId, COL_FLORA);
        editor->setColour (juce::TextEditor::outlineColourId, COL_MUTED);
        editor->setColour (juce::TextEditor::focusedOutlineColourId, COL_FLORA);
        editor->setColour (juce::CaretComponent::caretColourId, COL_FLORA);
        editor->onReturnKey = [this] { commitEditing(); };
        editor->onEscapeKey = [this] { cancelEditing(); };
        editor->onFocusLost = [this] { cancelEditing(); }; // egui: only lost_focus+Enter commits
        addChildComponent (*editor); // hidden until editing
    }

    void EditableName::setModelName (const juce::String& raw)
    {
        rawName = raw;
        if (! editing)
            repaint();
    }

    void EditableName::setEditingEnabled (bool enabled)
    {
        if (enabled == editingEnabled)
            return;
        editingEnabled = enabled;
        if (! enabled && editing)
            cancelEditing();
        setTooltip (enabled ? juce::String() : lockedTooltip);
    }

    void EditableName::startEditing()
    {
        if (! editingEnabled || editing)
            return;
        editing = true;
        editor->setText (rawName, juce::dontSendNotification); // edit the RAW name (not display/fallback)
        editor->setBounds (getLocalBounds());
        editor->setVisible (true);
        editor->toFront (false);
        editor->grabKeyboardFocus();
        editor->selectAll();
        repaint();
    }

    void EditableName::commitEditing()
    {
        if (! editing)
            return;
        editing = false;
        const juce::String txt = editor->getText();
        editor->setVisible (false);
        rawName = txt; // optimistic; FFI sanitizes its own copy (ASCII graphic+space ≤16 passes unchanged)
        if (onCommit)
            onCommit (txt);
        repaint();
    }

    void EditableName::cancelEditing()
    {
        if (! editing)
            return;
        editing = false;
        editor->setVisible (false); // discard the buffer (no commit)
        repaint();
    }

    void EditableName::mouseDown (const juce::MouseEvent&)
    {
        if (! editing)
            startEditing();
    }

    void EditableName::resized()
    {
        if (editing)
            editor->setBounds (getLocalBounds());
    }

    void EditableName::paint (juce::Graphics& g)
    {
        if (editing)
            return; // the TextEditor child paints itself

        const bool empty = rawName.isEmpty();
        const juce::String shown = prefix + (empty ? fallback : rawName);
        g.setFont (monoFont (14.0f));
        g.setColour (COL_FLORA);
        g.drawText (shown, getLocalBounds(), juce::Justification::centredLeft);
    }
}
