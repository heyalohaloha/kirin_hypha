#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaKeyLight.h"
#include "HyphaPresentationContext.h"
#include "HyphaReferenceHelpText.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

// B の画面の Balance の重ね比べの部品：線の色、ONE／ALL の切り替え、一覧の番号の箱（HyphaReferenceOverlay.h）。
namespace hypha::reference_ui
{
// 暗い地で見分けやすい 16 色。A の金・オレンジ・黄は使わない。先頭ほど互いに離れた色で、どれも番号の文字として
// 地と 4.5:1 以上。17 本目からは同じ並びを暗くして線に使う（番号と凡例の名前で分かる）。
inline constexpr std::array<std::uint32_t, 16> overlayPalette {
    0xff7fcfd8, 0xff8fd19a, 0xffb3a2e6, 0xffee97b5, 0xff9fd46a, 0xffe58fdb, 0xffa9b0c0, 0xff6e9bd0,
    0xff5fb8a0, 0xffc79ab0, 0xffb9e6c4, 0xffa8d8f5, 0xffc6da80, 0xff8c7fd0, 0xff4fa6b5, 0xfff2a3a8 };

inline juce::Colour overlayTextColour (int colour)
{
    return juce::Colour (overlayPalette[static_cast<size_t> (std::abs (colour)) % overlayPalette.size()]);
}

inline juce::Colour overlayLineColour (int colour)
{
    const auto base = overlayTextColour (colour);
    return colour < static_cast<int> (overlayPalette.size()) ? base : base.interpolatedWith (BG, 0.35f);
}

// ONE / ALL at the right of the Balance heading: two halves of one framed switch.
class OverlaySwitch final : public juce::Component
{
public:
    static constexpr int width = 96, height = 20;
    OverlaySwitch()
    {
        setComponentID ("reference-overlay");
        setTitle ("ONE / ALL");
        for (auto* half : { &one, &all })
        {
            half->setWantsKeyboardFocus (true);
            half->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            half->setTooltip (help_text::overlay);
            half->onClick = [this, half] { if (onChoose) onChoose (half == &all); };
            addAndMakeVisible (*half);
        }
        one.setComponentID ("reference-overlay-one");
        all.setComponentID ("reference-overlay-all");
    }
    std::function<void (bool)> onChoose;
    void setChoice (bool showsAll, presentation::Context next)
    {
        one.context = all.context = next;
        one.setToggleState (! showsAll, juce::dontSendNotification);
        all.setToggleState (showsAll, juce::dontSendNotification);
    }
    void paint (juce::Graphics& g) override
    {
        const key_light::Scope light (*this);
        surface_material::paintPanel (g, getLocalBounds().toFloat(), 0.82f, 4.0f);
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (1);
        one.setBounds (area.removeFromLeft (area.getWidth() / 2));
        all.setBounds (area);
    }

private:
    class Half final : public juce::Button
    {
    public:
        explicit Half (const char* text) : juce::Button (text) {}
        presentation::Context context = presentation::defaultContext();
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const key_light::Scope light (*this);
            if (getToggleState() || highlighted)
                surface_material::paintControl (g, getLocalBounds().toFloat().reduced (1.0f), highlighted, down,
                                                getToggleState(), COL_NORMAL, 3.0f);
            g.setColour (getToggleState() ? COL_NORMAL : COL_TEXT_SECONDARY);
            g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
            text_style::drawText (g, getName(), getLocalBounds(), juce::Justification::centred);
        }
    };
    Half one { "ONE" }, all { "ALL" };
};

// A row's number in ALL: a framed box in the line's colour while its line shows, plain while it does not.
class NumberToggle final : public juce::Button
{
public:
    NumberToggle() : juce::Button ("Line")
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (help_text::overlayNumber);
    }
    void show (int number, bool shown, int colour, presentation::Context next)
    {
        setName (juce::String (number));
        setTitle ("Line " + juce::String (number));
        setToggleable (true);
        setToggleState (shown, juce::dontSendNotification);
        lineColour = colour;
        context = next;
        repaint();
    }
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const key_light::Scope light (*this);
        const auto box = getLocalBounds().toFloat().withSizeKeepingCentre (juce::jmin (22.0f, getWidth() - 2.0f),
                                                                           juce::jmin (22.0f, getHeight() - 4.0f));
        const bool shown = getToggleState();
        const auto colour = shown ? overlayTextColour (lineColour) : COL_TEXT_SECONDARY;
        g.setColour ((shown ? overlayLineColour (lineColour) : COL_MUTED).withAlpha (shown ? (down ? 0.30f : 0.16f) : (highlighted ? 0.18f : 0.0f)));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (shown ? overlayLineColour (lineColour) : COL_MUTED.withAlpha (highlighted ? 0.95f : 0.62f));
        g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, shown ? 1.3f : 1.0f);
        g.setColour (colour);
        g.setFont (monoFont (context, typography::TextRole::unit, typography::Composition::information));
        text_style::drawText (g, getName(), box.toNearestInt(), juce::Justification::centred, false);
    }

private:
    int lineColour = 0;
    presentation::Context context = presentation::defaultContext();
};
}
