#include "HyphaWidgets.h"

#include "BinaryData.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"

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
        const auto bounds = getLocalBounds().toFloat();
        surface_material::paintControl (
            g, bounds.reduced (0.5f), highlighted, down, getToggleState(), COL_FLORA_BR);
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

    // ── EditableName ─────────────────────────────────────────────────────────────────────
    EditableName::EditableName()
    {
        setWantsKeyboardFocus (true);

        editor = std::make_unique<juce::TextEditor>();
        editor->setWantsKeyboardFocus (true);
        editor->setMultiLine (false);
        editor->setReturnKeyStartsNewLine (false);
        editor->setInputRestrictions (16, allowedNameChars()); // parity with sanitize_name (≤16)
        editor->setFont (monoFont (presentationContext, typography::TextRole::selector));
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
        const auto value = rawName.isEmpty() ? fallback : rawName;
        setTooltip ((editingEnabled ? enabledTooltip : lockedTooltip)
                    + (value.isNotEmpty() ? "\n" + prefix + value : juce::String {}));
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

    void EditableName::mouseDown (const juce::MouseEvent& event)
    {
        if (onSelect)
        {
            if (event.getNumberOfClicks() > 1) return;
            onSelect();
            return;
        }
        if (! editing)
            startEditing();
    }

    bool EditableName::setSelectionPreview (const juce::String& text, std::uint64_t generation)
    {
        const auto fits = text.isNotEmpty()
            && monoFont (presentationContext, typography::TextRole::selector).getStringWidthFloat (text) + 4 <= getWidth();
        const auto next = fits ? text : juce::String();
        const auto nextGeneration = fits ? generation : 0;
        if (selectionPreview != next || previewGeneration != nextGeneration)
        {
            selectionPreview = next; previewGeneration = nextGeneration; paintedPreview = 0; repaint();
        }
        return fits;
    }

    void EditableName::resized()
    {
        paintedPreview = 0;
        if (editing)
            editor->setBounds (getLocalBounds());
    }

    void EditableName::paint (juce::Graphics& g)
    {
        if (editing)
            return; // the TextEditor child paints itself

        const bool empty = rawName.isEmpty();
        const bool previewFits = selectionPreview.isNotEmpty()
            && monoFont (presentationContext, typography::TextRole::selector).getStringWidthFloat (selectionPreview) + 4 <= getWidth();
        const juce::String shown = previewFits ? selectionPreview : prefix + (empty ? fallback : rawName);
        paintedPreview = previewFits ? previewGeneration : 0;
        g.setFont (monoFont (presentationContext, typography::TextRole::selector));
        g.setColour (COL_FLORA);
        text_style::draw (g, shown, getLocalBounds(), presentationContext,
                          typography::TextRole::selector,
                          juce::Justification::centredLeft);
    }
}
