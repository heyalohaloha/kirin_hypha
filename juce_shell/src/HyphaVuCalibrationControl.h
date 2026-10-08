#pragma once

#include "HyphaVuCalibration.h"
#include "HyphaTextStyle.h"

namespace hypha::vu_calibration
{
// A small clickable calibration legend, with no knob or new row. The same reference applies to
// both needles; the measured values, peak rails and needle response stay in their own domains.
class Control final : public juce::Button
{
public:
    Control() : juce::Button ("0 VU")
    {
        setComponentID ("observatory-vu-calibration");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Set shared 0 VU. Audio, LUFS, TP and needle speed stay unchanged.");
        setButtonText (label (reference));
    }

    void setReference (int value)
    {
        const auto next = valid (value) ? value : defaultDbfs;
        if (reference == next) return;
        reference = next;
        setButtonText (label (reference));
    }
    int referenceDbfs() const noexcept { return reference; }
    void setPresentationContext (presentation::Context next)
    { context = next; repaint(); }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const bool active = isEnabled() && (highlighted || down || hasKeyboardFocus (true));
        const auto colour = active ? COL_NORMAL : COL_MUTED.withAlpha (0.78f);
        g.setColour (colour);
        text_style::draw (g, getButtonText(), getLocalBounds(), context,
                          typography::TextRole::legend, juce::Justification::centred,
                          1, typography::Composition::instrument);
        // A discreet underline reveals the click target only on hover / keyboard focus.
        if (active)
        {
            const auto width = text_style::shownWidth (
                labelFont (context, typography::TextRole::legend,
                           typography::Composition::instrument), getButtonText());
            const auto inset = (getWidth() - juce::jmin (width, static_cast<float> (getWidth()))) * 0.5f;
            g.drawHorizontalLine (getHeight() - 2, inset, getWidth() - inset);
        }
    }

private:
    int reference = defaultDbfs;
    presentation::Context context = presentation::defaultContext();
};
}
