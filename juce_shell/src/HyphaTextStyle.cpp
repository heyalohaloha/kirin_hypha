#include "HyphaTextStyle.h"

#include "HyphaLanguage.h"

#include <optional>

namespace hypha::text_style
{
juce::String shownText (const juce::String& text)
{
    return i18n::tr (text);
}

float shownWidth (const juce::Font& font, const juce::String& text)
{
    const auto shown = shownText (text);
    return requiresJapaneseGlyphs (shown) ? nativeTextFontLike (font).getStringWidthFloat (shown)
                                          : font.getStringWidthFloat (shown);
}

int requiredWidth (const juce::Font& font, const juce::String& text,
                   const typography::TextStyle& style, int minimum)
{
    return juce::jmax (minimum, juce::roundToInt (std::ceil (
        shownWidth (font, text) + 2.0f * style.horizontalPadding)));
}

int requiredLineHeight (const typography::TextStyle& style, int minimum) noexcept
{
    return juce::jmax (minimum, juce::roundToInt (std::ceil (style.lineHeight)));
}

juce::String ellipsizedText (const juce::String& text, const juce::Font& font, float width)
{
    if (width <= 0.0f || text.isEmpty()) return {};
    if (font.getStringWidthFloat (text) <= width)
        return text;
    const auto marker = juce::String::charToString (0x2026);
    if (font.getStringWidthFloat (marker) > width)
        return {};
    // Long source titles are painted repeatedly. Bound shaping work logarithmically
    // instead of measuring every one-character-shorter copy on every frame.
    int low = 0, high = text.length() - 1;
    while (low < high)
    {
        const auto length = low + (high - low + 1) / 2;
        const auto candidate = text.substring (0, length).trimEnd() + marker;
        if (font.getStringWidthFloat (candidate) <= width)
            low = length;
        else
            high = length - 1;
    }
    return text.substring (0, low).trimEnd() + marker;
}

float wrappedHeight (const juce::String& shown, const juce::Font& font, int width)
{
    // Japanese has no spaces to break at, and drawMultiLineText then breaks at the character that
    // overflows. Measure with the same line breaking so the block stays centred on its lines.
    if (requiresJapaneseGlyphs (shown))
    {
        juce::GlyphArrangement arrangement;
        arrangement.addJustifiedText (font, shown, 0.0f, 0.0f, static_cast<float> (width),
                                      juce::Justification::left, font.getHeight() * 0.2f);
        return arrangement.getNumGlyphs() > 0
            ? arrangement.getBoundingBox (0, -1, true).getHeight() : 0.0f;
    }
    juce::AttributedString attributed;
    attributed.append (shown, font, juce::Colours::white);
    juce::TextLayout layout;
    layout.createLayout (attributed, static_cast<float> (width));
    return layout.getHeight();
}

namespace
{
// Japanese text needs the native font; the caller's font and everything else it set come back
// when the scope ends. English leaves the context untouched.
class ShownFont final
{
public:
    ShownFont (juce::Graphics& graphics, const juce::String& shown)
    {
        if (! requiresJapaneseGlyphs (shown))
            return;
        saved.emplace (graphics);
        graphics.setFont (nativeTextFontLike (graphics.getCurrentFont()));
    }

private:
    std::optional<juce::Graphics::ScopedSaveState> saved;
};

float lineLeading (const juce::String& text, const juce::Font& font)
{
    return requiresJapaneseGlyphs (text) ? font.getHeight() * 0.2f : 0.0f;
}

void drawWrapped (juce::Graphics& graphics, const juce::String& text,
                  juce::Rectangle<int> area, juce::Justification justification)
{
    const auto font = graphics.getCurrentFont();
    const auto height = wrappedHeight (text, font, area.getWidth());
    // The block keeps the vertical justification asked for: a label set to the top stays there.
    const auto free = juce::jmax (0.0f, static_cast<float> (area.getHeight()) - height);
    const auto offset = justification.testFlags (juce::Justification::top) ? 0.0f
                      : justification.testFlags (juce::Justification::bottom) ? free
                                                                             : free * 0.5f;
    const auto baseline = area.getY() + juce::roundToInt (offset + font.getAscent());
    // drawMultiLineText justifies each line inside [x, x + width], so the box starts at the area's
    // left edge whatever the justification. Starting it at the centre put a centred status half
    // outside the area, where the clip cut "INACTIVE" to "INAC".
    juce::Graphics::ScopedSaveState saved (graphics);
    graphics.reduceClipRegion (area);
    graphics.drawMultiLineText (text, area.getX(), baseline, area.getWidth(), justification,
                                lineLeading (text, font));
}

template <typename Area>
void drawShown (juce::Graphics& graphics, const juce::String& text, Area area,
                juce::Justification justification, bool useEllipsesIfTooLong)
{
    const auto shown = shownText (text);
    const ShownFont font (graphics, shown);
    graphics.drawText (shown, area, justification, useEllipsesIfTooLong);
}
}

void draw (juce::Graphics& graphics, const juce::String& text,
           juce::Rectangle<int> area, const presentation::Context& context,
           typography::TextRole role, juce::Justification justification,
           int maximumLines, typography::Composition composition)
{
    if (area.isEmpty() || text.isEmpty())
        return;
    const auto shown = shownText (text);
    const ShownFont font (graphics, shown);
    const auto overflow = typography::resolve (context, role, composition).overflow;
    if (overflow == typography::Overflow::wrap && maximumLines > 1)
    {
        drawWrapped (graphics, shown, area, justification);
        return;
    }
    const auto displayed = overflow == typography::Overflow::ellipsize
        ? ellipsizedText (shown, graphics.getCurrentFont(), static_cast<float> (area.getWidth()))
        : shown;
    graphics.drawText (displayed, area, justification, false);
}

void drawEllipsized (juce::Graphics& graphics, const juce::String& text,
                     juce::Rectangle<int> area, juce::Justification justification)
{
    if (area.isEmpty() || text.isEmpty()) return;
    const auto shown = shownText (text);
    const ShownFont font (graphics, shown);
    graphics.drawText (ellipsizedText (shown, graphics.getCurrentFont(),
                                       static_cast<float> (area.getWidth())),
                       area, justification, false);
}

void drawLines (juce::Graphics& graphics, const juce::String& text, juce::Rectangle<int> area,
                juce::Justification justification, int maximumLines)
{
    if (area.isEmpty() || text.isEmpty()) return;
    const auto shown = shownText (text);
    const ShownFont font (graphics, shown);
    if (maximumLines > 1)
        drawWrapped (graphics, shown, area, justification);
    else
        graphics.drawText (shown, area, justification, true);
}

void drawText (juce::Graphics& graphics, const juce::String& text, juce::Rectangle<int> area,
               juce::Justification justification, bool useEllipsesIfTooLong)
{
    drawShown (graphics, text, area, justification, useEllipsesIfTooLong);
}

void drawText (juce::Graphics& graphics, const juce::String& text, juce::Rectangle<float> area,
               juce::Justification justification, bool useEllipsesIfTooLong)
{
    drawShown (graphics, text, area, justification, useEllipsesIfTooLong);
}

void drawText (juce::Graphics& graphics, const juce::String& text, int x, int y, int width,
               int height, juce::Justification justification, bool useEllipsesIfTooLong)
{
    drawShown (graphics, text, juce::Rectangle<int> (x, y, width, height), justification,
               useEllipsesIfTooLong);
}
}
