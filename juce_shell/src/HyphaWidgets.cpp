#include "HyphaWidgets.h"

#include "BinaryData.h"

#include <cmath>

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
        setInterceptsMouseClicks (true, false);
        setTooltip ("No active signal.");
    }

    void StatusLed::setState (LedState s)
    {
        if (s != state)
        {
            state = s;
            switch (state)
            {
                case LedState::Idle:            setTooltip ("No active signal."); break;
                case LedState::Error:           setTooltip ("Measurement is unavailable."); break;
                case LedState::RecordStandby:   setTooltip ("Keep is waiting for its pair."); break;
                case LedState::WatchBreathing:  setTooltip ("Measurement is active."); break;
                case LedState::RecordActive:    setTooltip ("Keep is recording."); break;
                case LedState::PresetAvailable: setTooltip ("A kept result is available."); break;
            }
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

    // ── PairDropdownButton ───────────────────────────────────────────────────────────────
    PairDropdownButton::PairDropdownButton()
        : juce::TextButton ("Pair and Keep menu")
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void PairDropdownButton::paintButton (
        juce::Graphics& g, bool highlighted, bool down)
    {
        const auto background = findColour (getToggleState()
                                                ? juce::TextButton::buttonOnColourId
                                                : juce::TextButton::buttonColourId,
                                            true);
        getLookAndFeel().drawButtonBackground (g, *this, background, highlighted, down);

        const auto bounds = getLocalBounds().toFloat();
        const float centreX = bounds.getCentreX();
        const float centreY = bounds.getCentreY() + (down ? 1.0f : 0.0f);
        constexpr float halfWidth = 4.0f;
        constexpr float halfHeight = 2.5f;
        juce::Path arrow;
        arrow.addTriangle (centreX - halfWidth, centreY - halfHeight,
                           centreX + halfWidth, centreY - halfHeight,
                           centreX, centreY + halfHeight);
        g.setColour (isEnabled()
                         ? findColour (getToggleState()
                                           ? juce::TextButton::textColourOnId
                                           : juce::TextButton::textColourOffId,
                                       true)
                         : COL_MUTED);
        g.fillPath (arrow);
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
        // contract-owned horizontal spacing, all left-aligned). label | value | unit.
        // The larger release typography still fits the fixed 300px editor because spacing is
        // part of the same shared contract as the font and two-column geometry.
        const float spacing = ui_contract::metricHorizontalSpacing;
        const auto  labelF = labelFont (labelSize);
        const auto  valueF = monoFont  (valueSize);
        const auto  unitF  = labelFont (unitSize);

        const float labelW = juce::jmax (minColW, labelF.getStringWidthFloat (label));
        const float valueW = juce::jmax (minColW, tabularTextWidth (valueF, value));

        const float h = (float) getHeight();
        float x = 0.0f;

        g.setFont (labelF);
        g.setColour (COL_MUTED);
        g.drawText (label, juce::Rectangle<float> (x, 0.0f, labelW, h), juce::Justification::centredLeft);
        x += labelW + spacing;

        g.setColour (valueColour);
        drawTabularText (g, valueF, value, juce::Rectangle<float> (x, 0.0f, valueW, h),
                         juce::Justification::centredLeft);
        x += valueW + spacing;

        g.setFont (unitF);
        g.setColour (COL_MUTED);
        g.drawText (unit, juce::Rectangle<float> (x, 0.0f, juce::jmax (0.0f, (float) getWidth() - x), h),
                    juce::Justification::centredLeft);
    }

    // ── M/S display selector ─────────────────────────────────────────────────────────────
    LoudnessSelector::SegmentButton::SegmentButton (const juce::String& textIn)
        : juce::Button (textIn + " loudness"), text (textIn)
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void LoudnessSelector::SegmentButton::setSelected (bool selectedIn)
    {
        if (selected == selectedIn)
            return;
        selected = selectedIn;
        repaint();
    }

    void LoudnessSelector::SegmentButton::paintButton (
        juce::Graphics& g, bool highlighted, bool down)
    {
        auto area = getLocalBounds().toFloat();
        if (selected || highlighted || down)
        {
            g.setColour (selected ? kFieldFill.brighter (0.08f) : kFieldFill);
            g.fillRect (area);
        }
        g.setColour (selected ? COL_FLORA : COL_MUTED);
        g.setFont (monoFont (ui_contract::metricLabelFontHeight));
        g.drawText (text, getLocalBounds(), juce::Justification::centred);
        if (hasKeyboardFocus (true))
        {
            g.setColour (COL_FLORA);
            g.drawRect (area, 1.0f);
        }
    }

    LoudnessSelector::LoudnessSelector()
    {
        momentary.setTitle ("Momentary loudness");
        momentary.setDescription ("Show the 400 millisecond Momentary loudness measurement");
        shortTerm.setTitle ("Short-term loudness");
        shortTerm.setDescription ("Show the 3 second Short-term loudness measurement");
        momentary.setTooltip (helpLufsM());
        shortTerm.setTooltip (helpLufsS());
        momentary.onClick = [this]
        {
            setShortTerm (false);
            if (onChange) onChange (false);
        };
        shortTerm.onClick = [this]
        {
            setShortTerm (true);
            if (onChange) onChange (true);
        };
        addAndMakeVisible (momentary);
        addAndMakeVisible (shortTerm);
        setShortTerm (false);
    }

    void LoudnessSelector::setShortTerm (bool shortTermIn)
    {
        selectedShortTerm = shortTermIn;
        momentary.setSelected (! selectedShortTerm);
        shortTerm.setSelected (selectedShortTerm);
    }

    void LoudnessSelector::setDeltaMode (bool delta)
    {
        if (deltaMode == delta)
            return;
        deltaMode = delta;
        resized();
        repaint();
    }

    ui_contract::LoudnessSelectorLayout LoudnessSelector::currentLayout() const
    {
        const auto font = labelFont (ui_contract::metricLabelFontHeight);
        const int measuredGlyphWidth = static_cast<int> (
            std::ceil (font.getStringWidthFloat (delta())));
        return ui_contract::loudnessSelectorLayout (
            deltaMode, measuredGlyphWidth, getWidth(), getHeight());
    }

    void LoudnessSelector::paint (juce::Graphics& g)
    {
        const auto layout = currentLayout();
        if (deltaMode)
        {
            g.setColour (COL_MUTED);
            g.setFont (labelFont (ui_contract::metricLabelFontHeight));
            g.drawFittedText (delta(), 0, 0, layout.deltaPrefixWidth, getHeight(),
                              juce::Justification::centredLeft, 1, 0.75f);
        }
        g.setColour (COL_MUTED);
        g.drawRect ((float) layout.deltaPrefixWidth, 3.0f,
                    (float) juce::jmax (0, getWidth() - layout.deltaPrefixWidth),
                    (float) juce::jmax (0, getHeight() - 6), 1.0f);
    }

    void LoudnessSelector::resized()
    {
        const auto layout = currentLayout();
        momentary.setBounds (layout.momentary.x, layout.momentary.y,
                             layout.momentary.width, layout.momentary.height);
        shortTerm.setBounds (layout.shortTerm.x, layout.shortTerm.y,
                             layout.shortTerm.width, layout.shortTerm.height);
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
        editor->setFont (monoFont (ui_contract::nameFontHeight));
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
        setTooltip (enabled ? enabledTooltip : lockedTooltip);
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
        g.setFont (monoFont (ui_contract::nameFontHeight));
        g.setColour (COL_FLORA);
        g.drawFittedText (shown, getLocalBounds(), juce::Justification::centredLeft,
                          1, 0.72f);
    }
}
