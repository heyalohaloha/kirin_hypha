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

namespace
{
// Japanese line breaking: a line never starts with closing punctuation or a small kana, never
// ends with an opening bracket, and a Latin word ("Kirin OS", "PRE") never breaks inside.
bool opensNoLine (juce::juce_wchar character) noexcept
{
    static const auto marks = juce::String::fromUTF8 (
        u8"、。，．・：；？！ー」』）】〕〉》…‥ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ");
    return marks.containsChar (character) || juce::String (",.)]!?:;").containsChar (character);
}

bool closesNoLine (juce::juce_wchar character) noexcept
{
    static const auto marks = juce::String::fromUTF8 (u8"「『（【〔〈《");
    return marks.containsChar (character) || character == '(' || character == '[';
}

bool latinWord (juce::juce_wchar character) noexcept
{
    return character != ' ' && character < 0x2e80;
}

// The pieces a line may break between: one kana or kanji at a time, a whole Latin word, with
// the punctuation and brackets that must stay beside them.
juce::StringArray breakUnits (const juce::String& paragraph)
{
    juce::StringArray units;
    juce::String unit;
    juce::juce_wchar previous = 0;
    for (auto cursor = paragraph.getCharPointer(); ! cursor.isEmpty(); ++cursor)
    {
        const auto character = *cursor;
        const auto joins = unit.isNotEmpty()
            && (opensNoLine (character) || closesNoLine (previous)
                || (latinWord (character) && latinWord (previous)));
        if (! joins && unit.isNotEmpty())
        {
            units.add (unit);
            unit.clear();
        }
        unit += juce::String::charToString (character);
        if (character == ' ')
        {
            units.add (unit);
            unit.clear();
        }
        previous = character;
    }
    if (unit.isNotEmpty())
        units.add (unit);
    return units;
}

}

juce::StringArray japaneseLines (const juce::String& text, const juce::Font& font, float width)
{
    juce::StringArray lines;
    juce::StringArray paragraphs;
    paragraphs.addTokens (text, "\n", {});
    const auto fits = [&font, width] (const juce::String& line)
    { return font.getStringWidthFloat (line.trimEnd()) <= width; };
    for (const auto& paragraph : paragraphs)
    {
        const auto first = lines.size();
        juce::String line;
        for (const auto& unit : breakUnits (paragraph))
        {
            if (line.isNotEmpty() && ! fits (line + unit))
            {
                lines.add (line.trimEnd());
                line.clear();
            }
            line += unit;
            // A piece wider than the whole line (a long Latin word) breaks where it overflows.
            while (! fits (line) && line.length() > 1)
            {
                auto length = line.length() - 1;
                while (length > 1 && ! fits (line.substring (0, length)))
                    --length;
                lines.add (line.substring (0, length));
                line = line.substring (length);
            }
        }
        lines.add (line.trimEnd());
        // A last line of a few characters ("す。") reads as a broken word: when the line before
        // holds a sentence or clause break, the paragraph breaks there instead, if both still fit.
        const auto last = lines.size() - 1;
        if (last > first && font.getStringWidthFloat (lines[last]) < width * 0.3f)
        {
            const auto& previous = lines.getReference (last - 1);
            static const auto clauseEnds = juce::String::fromUTF8 (u8"。、！？ ");
            for (auto split = previous.length() - 1; split > 0; --split)
            {
                if (! clauseEnds.containsChar (previous[split - 1]))
                    continue;
                const auto moved = previous.substring (split).trimStart() + lines[last];
                if (! fits (moved))
                    break;
                lines.set (last, moved);
                lines.set (last - 1, previous.substring (0, split).trimEnd());
                break;
            }
        }
    }
    return lines;
}

namespace
{
float lineLeading (const juce::Font& font)
{
    return font.getHeight() * 0.2f;
}
}

float wrappedHeight (const juce::String& shown, const juce::Font& font, int width)
{
    // Japanese is broken into lines here, by the rules above, and drawn line by line: the height
    // is those lines with a fifth of a line between them.
    if (requiresJapaneseGlyphs (shown))
    {
        const auto count = japaneseLines (shown, font, static_cast<float> (width)).size();
        return count > 0 ? static_cast<float> (count) * font.getHeight()
                               + static_cast<float> (count - 1) * lineLeading (font)
                         : 0.0f;
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
    juce::Graphics::ScopedSaveState saved (graphics);
    graphics.reduceClipRegion (area);
    if (requiresJapaneseGlyphs (text))
    {
        const auto row = font.getHeight() + lineLeading (font);
        auto top = static_cast<float> (area.getY()) + offset;
        for (const auto& line : japaneseLines (text, font, static_cast<float> (area.getWidth())))
        {
            graphics.drawText (line, juce::Rectangle<float> (static_cast<float> (area.getX()), top,
                                                             static_cast<float> (area.getWidth()),
                                                             font.getHeight()),
                               justification.getOnlyHorizontalFlags()
                                   | juce::Justification::verticallyCentred,
                               false);
            top += row;
        }
        return;
    }
    // drawMultiLineText justifies each line inside [x, x + width], so the box starts at the area's
    // left edge whatever the justification. Starting it at the centre put a centred status half
    // outside the area, where the clip cut "INACTIVE" to "INAC".
    const auto baseline = area.getY() + juce::roundToInt (offset + font.getAscent());
    graphics.drawMultiLineText (text, area.getX(), baseline, area.getWidth(), justification);
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
