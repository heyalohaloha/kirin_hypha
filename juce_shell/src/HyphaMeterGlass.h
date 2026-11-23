#pragma once

#include "HyphaTextStyle.h"
#include "HyphaTheme.h"
#include <array>
#include <cmath>

// The glass of the photographed meter and the rows of words that stand on it, shared by the LU, PSR, CREST
// and SHARP faces.
namespace hypha::metric_face
{
// The lit glass of both photographs, in their common 1536 x 1024 coordinates.
namespace glass
{
inline constexpr float width = 1536.0f, height = 1024.0f;
inline constexpr float bottom = 684.0f;  // the dark lip above the flat gold rim
inline constexpr float hubRadius = 72.0f, hubRow = 622.0f;
inline constexpr std::array<float, 2> pairedHubs { 420.0f, 1110.0f };
inline constexpr float singleHub = 766.0f;
// How far the rim's inner edge stands from the photograph's left (and right) edge at a row: the
// lower corners curve in, so a row of words is as wide as its lower edge allows.
inline float inset (float row) noexcept
{
    constexpr std::array<std::array<float, 2>, 10> edge {{
        {{ 540, 66 }}, {{ 564, 72 }}, {{ 596, 84 }}, {{ 628, 98 }}, {{ 644, 114 }},
        {{ 660, 135 }}, {{ 668, 154 }}, {{ 676, 170 }}, {{ 684, 200 }}, {{ 686, 210 }} }};
    if (row <= edge.front()[0]) return edge.front()[1];
    for (std::size_t i = 1; i < edge.size(); ++i)
        if (row <= edge[i][0])
            return juce::jmap (row, edge[i - 1][0], edge[i][0], edge[i - 1][1], edge[i][1]);
    return edge.back()[1];
}
inline float x (juce::Rectangle<int> chassis, float asset) noexcept
{ return static_cast<float> (chassis.getX()) + asset * static_cast<float> (chassis.getWidth()) / width; }
inline float y (juce::Rectangle<int> chassis, float asset) noexcept
{ return static_cast<float> (chassis.getY()) + asset * static_cast<float> (chassis.getHeight()) / height; }
inline float row (juce::Rectangle<int> chassis, float pixel) noexcept
{ return (pixel - static_cast<float> (chassis.getY())) * height / static_cast<float> (chassis.getHeight()); }
// The glass between two asset columns, over whole pixel rows.
inline juce::Rectangle<int> band (juce::Rectangle<int> chassis, float left, float right, int top, int bottomPixel)
{
    const auto x0 = static_cast<int> (std::ceil (x (chassis, left))), x1 = static_cast<int> (std::floor (x (chassis, right)));
    return juce::Rectangle<int>::leftTopRightBottom (x0, top, juce::jmax (x0, x1), bottomPixel);
}
}

// Shown text in at most two rows of `width`: Japanese breaks between its phrases, other text at a space.
inline std::array<juce::String, 2> twoRows (const juce::String& text, const juce::Font& font, float width)
{
    if (requiresJapaneseGlyphs (text))
    {
        const auto native = nativeTextFontLike (font);
        const auto lines = text_style::japaneseLines (text, native, width);
        if (lines.size() <= 2) return {lines[0], lines[1]};
        // Phrases that need three rows here break after the last particle that lets both rows fit.
        const auto particles = juce::String::fromUTF8 (u8"がはをのにでと");
        for (int at = text.length() - 1; at > 1; --at)
            if (particles.containsChar (text[at - 1]) && native.getStringWidthFloat (text.substring (0, at)) <= width
                && native.getStringWidthFloat (text.substring (at)) <= width)
                return {text.substring (0, at), text.substring (at)};
        return {lines[0], lines.joinIntoString ("", 1)};
    }
    const auto words = juce::StringArray::fromTokens (text, " ", {});
    juce::String first;
    int next = 0;
    while (next < words.size() && (first.isEmpty() || font.getStringWidthFloat (first + " " + words[next]) <= width))
        first << (first.isEmpty() ? "" : " ") << words[next++];
    return {first, words.joinIntoString (" ", next)};
}
}
