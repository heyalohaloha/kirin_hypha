#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

// Every Hypha popup menu and combo list is drawn like Kirin OS's Kirin Select on its Reference
// screen: a near-black warm window with a thin edge, a faint inner light above and copper below,
// rounded rows, the chosen row marked by a champagne bar and check, and small gold capitals for
// section headers. One drawing for all menus, so the PAIR, MENU and REF lists cannot drift apart.
namespace hypha::select_menu
{
inline const juce::Colour window    { 0xf70a0806 }; // rgba(10, 8, 6, 0.97)
inline const juce::Colour edge      { 0x33968c80 }; // rgba(150, 140, 128, 0.2)
inline const juce::Colour topLight  { 0x38f0e4cc }; // rgba(240, 228, 204, 0.22)
inline const juce::Colour lowCopper { 0x29d0835a }; // rgba(208, 131, 90, 0.16)
inline const juce::Colour ink       { 0xffd8cfc2 };
inline const juce::Colour ivory     { 0xfff0e4cc }; // chosen and pointed rows
inline const juce::Colour activeRow { 0x0bf0e4cc }; // rgba(240, 228, 204, 0.045)
inline const juce::Colour champagne { 0xffe0bd7e }; // the chosen row's bar and check
inline const juce::Colour goldLabel { 0xffb18d4f }; // section headers

constexpr float windowRadius = 9.0f;
constexpr float rowRadius = 6.0f;
constexpr int border = 6;       // the window's padding around the rows
constexpr int rowHeight = 34;   // Kirin Select's row; the 16 px menu font sits in it with air
constexpr int textInset = 11;   // after the 2 px bar
constexpr int trailing = 26;    // the check or submenu arrow
constexpr int separatorHeight = 9;

// JUCE gives a popup a transparent window only when its background colour is transparent and the
// platform supports it; otherwise the window is opaque and the corners must be filled too.
inline bool roundedWindow (const juce::LookAndFeel& look)
{
    return look.findColour (juce::PopupMenu::backgroundColourId).isTransparent()
        && juce::Desktop::canUseSemiTransparentWindows();
}

inline void paintWindow (juce::Graphics& g, juce::Rectangle<float> area, bool rounded)
{
    const auto radius = rounded ? windowRadius : 0.0f;
    if (! rounded) g.fillAll (window.withAlpha (1.0f));
    juce::Path shape;
    shape.addRoundedRectangle (area.reduced (0.5f), radius);
    g.setColour (rounded ? window : window.withAlpha (1.0f));
    g.fillPath (shape);
    const auto inner = area.reduced (radius * 0.6f + 1.0f, 1.0f);
    g.setColour (topLight);
    g.fillRect (inner.getX(), area.getY() + 1.0f, inner.getWidth(), 1.0f);
    g.setColour (lowCopper);
    g.fillRect (inner.getX(), area.getBottom() - 2.0f, inner.getWidth(), 1.0f);
    g.setColour (edge);
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}

inline void paintCheck (juce::Graphics& g, juce::Rectangle<float> box)
{
    const auto size = juce::jmin (box.getWidth(), box.getHeight(), 12.0f);
    const auto r = box.withSizeKeepingCentre (size, size);
    juce::Path check;
    check.startNewSubPath (r.getX(), r.getCentreY());
    check.lineTo (r.getX() + size * 0.38f, r.getBottom() - size * 0.18f);
    check.lineTo (r.getRight(), r.getY() + size * 0.15f);
    g.setColour (champagne);
    g.strokePath (check, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

inline void paintArrow (juce::Graphics& g, juce::Rectangle<float> box)
{
    const auto r = box.withSizeKeepingCentre (6.0f, 10.0f);
    juce::Path arrow;
    arrow.startNewSubPath (r.getX(), r.getY());
    arrow.lineTo (r.getRight(), r.getCentreY());
    arrow.lineTo (r.getX(), r.getBottom());
    g.setColour (champagne.withAlpha (0.7f));
    g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::mitered,
                                               juce::PathStrokeType::rounded));
}

struct Row
{
    bool separator = false, active = true, pointed = false, chosen = false, submenu = false;
    juce::String text, shortcut;
    const juce::Colour* colour = nullptr;
};

inline void paintRow (juce::Graphics& g, juce::Rectangle<int> area, const Row& row,
                      const juce::Font& font)
{
    const auto bounds = area.toFloat();
    if (row.separator)
    {
        g.setColour (edge);
        g.fillRect (bounds.getX() + (float) textInset, bounds.getCentreY(),
                    bounds.getWidth() - 2.0f * (float) textInset, 1.0f);
        return;
    }
    if (row.pointed && row.active)
    {
        g.setColour (activeRow);
        g.fillRoundedRectangle (bounds, rowRadius);
    }
    if (row.chosen)
    {
        g.setColour (champagne);
        g.fillRoundedRectangle (bounds.withWidth (2.0f).reduced (0.0f, 7.0f), 1.0f);
    }
    auto text = area.withTrimmedLeft (textInset).withTrimmedRight (trailing);
    const auto colour = row.colour != nullptr ? *row.colour
                      : row.pointed || row.chosen ? ivory : ink;
    g.setFont (nativeTextFontLike (font));
    if (row.shortcut.isNotEmpty())
    {
        const auto shortcutWidth = juce::jmin (text.getWidth() / 3,
            font.getStringWidth (row.shortcut) + 8);
        g.setColour (ink.withMultipliedAlpha (row.active ? 0.6f : 0.25f));
        text_style::drawText (g, row.shortcut, text.removeFromRight (shortcutWidth),
                              juce::Justification::centredRight);
    }
    g.setColour (colour.withMultipliedAlpha (row.active ? 1.0f : 0.4f));
    text_style::drawText (g, row.text, text, juce::Justification::centredLeft);
    const auto end = bounds.withLeft (bounds.getRight() - (float) trailing);
    if (row.submenu) paintArrow (g, end);
    else if (row.chosen) paintCheck (g, end);
}

// Small gold capitals in the measurement face, like Kirin Select's group labels.
inline void paintHeader (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
{
    g.setFont (monoFont (presentation::forOutput (300, 200, presentation::OutputTarget::editor),
                         typography::TextRole::axis).withExtraKerningFactor (0.12f));
    g.setColour (goldLabel);
    text_style::drawText (g, text.toUpperCase(), area.withTrimmedLeft (textInset).withTrimmedRight (textInset)
                              .withTrimmedTop (area.getHeight() / 3),
                          juce::Justification::centredLeft);
}

inline void idealRowSize (const juce::String& text, bool separator, int standardHeight,
                          const juce::Font& font, int& width, int& height)
{
    if (separator)
    {
        width = 50;
        height = separatorHeight;
        return;
    }
    height = juce::jmax (rowHeight, standardHeight, juce::roundToInt (font.getHeight() * 1.6f));
    width = font.getStringWidth (text) + textInset + trailing + 8;
}
}
