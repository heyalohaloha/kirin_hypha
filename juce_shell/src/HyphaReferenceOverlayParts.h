#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaKeyLight.h"
#include "HyphaPresentationContext.h"
#include "HyphaReferenceHelpText.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"
#include "reference_audition/ReferenceVisualPreferences.h"

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

// ONE / ALL at the right of the Balance heading: two halves of one framed switch. In ALL, how it draws
// stands to its left as a second switch of three: LINES (each line turned on), SHAPE (their mean and range)
// and ALL SHAPE (the mean and range of every song and target, whatever is turned on).
class OverlaySwitch final : public juce::Component
{
public:
    using Drawing = reference_audition::BalanceOverlay::Drawing;
    static constexpr int width = 96, height = 20, gap = 8;
    static constexpr std::array<const char*, 3> drawingNames { "LINES", "SHAPE", "ALL SHAPE" };
    // The drawing switch's parts, as wide as their words in the page's font.
    static std::array<int, 3> drawingWidths (presentation::Context context)
    {
        const auto font = labelFont (context, typography::TextRole::unit, typography::Composition::information);
        std::array<int, 3> widths {};
        for (size_t index = 0; index < widths.size(); ++index)
            widths[index] = juce::roundToInt (std::ceil (text_style::shownWidth (font, drawingNames[index]))) + 16;
        return widths;
    }
    static int widthFor (bool all, presentation::Context context)
    {
        if (! all) return width;
        const auto parts = drawingWidths (context);
        return parts[0] + parts[1] + parts[2] + 2 + gap + width;
    }
    OverlaySwitch()
    {
        setComponentID ("reference-overlay");
        setTitle ("ONE / ALL");
        static constexpr std::array<const char*, 3> ids { "reference-overlay-lines", "reference-overlay-shape", "reference-overlay-all-shape" };
        for (size_t index = 0; index < drawing.size(); ++index)
        {
            drawing[index] = std::make_unique<Half> (drawingNames[index]);
            drawing[index]->setComponentID (ids[index]);
            drawing[index]->onClick = [this, index] { if (onDrawing) onDrawing (static_cast<Drawing> (index)); };
        }
        for (auto* half : halves())
        {
            half->setWantsKeyboardFocus (true);
            half->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            half->setTooltip (half == &one || half == &all ? help_text::overlay : help_text::drawing);
            addAndMakeVisible (*half);
        }
        one.onClick = [this] { if (onChoose) onChoose (false); };
        all.onClick = [this] { if (onChoose) onChoose (true); };
        one.setComponentID ("reference-overlay-one");
        all.setComponentID ("reference-overlay-all");
    }
    std::function<void (bool)> onChoose;
    std::function<void (Drawing)> onDrawing;
    void setChoice (bool showsAll, Drawing shown, presentation::Context next)
    {
        context = next;
        for (auto* half : halves()) half->context = next;
        one.setToggleState (! showsAll, juce::dontSendNotification);
        all.setToggleState (showsAll, juce::dontSendNotification);
        for (size_t index = 0; index < drawing.size(); ++index)
        {
            drawing[index]->setToggleState (static_cast<Drawing> (index) == shown, juce::dontSendNotification);
            drawing[index]->setVisible (showsAll);
        }
        resized();
    }
    void paint (juce::Graphics& g) override
    {
        const key_light::Scope light (*this);
        auto area = getLocalBounds();
        surface_material::paintPanel (g, area.removeFromRight (width).toFloat(), 0.82f, 4.0f);
        if (drawing[0]->isVisible())
            surface_material::paintPanel (g, area.withTrimmedRight (gap).toFloat(), 0.82f, 4.0f);
    }
    void resized() override
    {
        auto area = getLocalBounds();
        auto pair = area.removeFromRight (width).reduced (1);
        one.setBounds (pair.removeFromLeft (pair.getWidth() / 2));
        all.setBounds (pair);
        area.removeFromRight (gap);
        area = area.reduced (1);
        const auto widths = drawingWidths (context);
        for (size_t index = 0; index < drawing.size(); ++index)
            drawing[index]->setBounds (index + 1 == drawing.size() ? area : area.removeFromLeft (widths[index]));
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
    std::array<Half*, 5> halves() { return { &one, &all, drawing[0].get(), drawing[1].get(), drawing[2].get() }; }
    Half one { "ONE" }, all { "ALL" };
    std::array<std::unique_ptr<Half>, 3> drawing;
    presentation::Context context = presentation::defaultContext();
};

// What a number says in ALL SHAPE, which takes every song and target whatever the numbers say.
inline constexpr const char* allShapeFixedText = "ALL SHAPE uses every song. Choose SHAPE to pick songs.";

// A row's number in ALL: a framed box in the line's colour while its line shows, plain while it does not.
// In ALL SHAPE the boxes are dimmed (their text stays readable) and say why a press changes nothing.
class NumberToggle final : public juce::Button
{
public:
    NumberToggle() : juce::Button ("Line")
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (help_text::overlayNumber);
    }
    void show (int number, bool shown, int colour, presentation::Context next, bool fixed = false)
    {
        dimmed = fixed;
        setTooltip (fixed ? allShapeFixedText : help_text::overlayNumber);
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
        // Dimmed: the frame keeps a quiet hint of the line's colour; the number stays in the readable text tier.
        const auto colour = shown && ! dimmed ? overlayTextColour (lineColour) : COL_TEXT_SECONDARY;
        if (! dimmed)
        {
            g.setColour ((shown ? overlayLineColour (lineColour) : COL_MUTED).withAlpha (shown ? (down ? 0.30f : 0.16f) : (highlighted ? 0.18f : 0.0f)));
            g.fillRoundedRectangle (box, 3.0f);
        }
        g.setColour (dimmed ? (shown ? overlayLineColour (lineColour).interpolatedWith (BG, 0.55f) : COL_MUTED.withAlpha (0.40f))
                            : shown ? overlayLineColour (lineColour) : COL_MUTED.withAlpha (highlighted ? 0.95f : 0.62f));
        g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, shown && ! dimmed ? 1.3f : 1.0f);
        g.setColour (colour);
        g.setFont (monoFont (context, typography::TextRole::unit, typography::Composition::information));
        text_style::drawText (g, getName(), box.toNearestInt(), juce::Justification::centred, false);
    }

private:
    int lineColour = 0;
    bool dimmed = false;
    presentation::Context context = presentation::defaultContext();
};
}
