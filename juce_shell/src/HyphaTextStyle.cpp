#include "HyphaTextStyle.h"

#include "HyphaLanguage.h"

#include <optional>

namespace hypha::text_style
{
namespace
{
thread_local ShownTextLog* activeLog = nullptr;

// Every drawing call reports the text it drew here (nothing happens unless a test keeps a log).
const juce::String& drawn (const juce::String& text, juce::Rectangle<float> area)
{
    if (activeLog != nullptr && text.isNotEmpty()) activeLog->add (text, area);
    return text;
}
}

ShownTextLog::ShownTextLog() noexcept : outer (activeLog)
{
    activeLog = this;
}

ShownTextLog::~ShownTextLog()
{
    activeLog = outer;
}

juce::String shownText (const juce::String& text, LabelPolicy policy)
{
    return policy == LabelPolicy::fixed ? text : i18n::tr (text);
}

float shownWidth (const juce::Font& font, const juce::String& text, LabelPolicy policy)
{
    const auto shown = shownText (text, policy);
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

juce::String ellipsizedKeepingVersion (const juce::String& text, const juce::Font& font, float width)
{
    if (width <= 0.0f || font.getStringWidthFloat (text) <= width)
        return ellipsizedText (text, font, width);
    const auto separator = juce::String::fromUTF8 (u8" · v");
    const auto split = text.lastIndexOf (separator);
    const auto number = split > 0 ? text.substring (split + separator.length()) : juce::String();
    if (number.isNotEmpty() && number.length() <= 4 && number.containsOnly ("0123456789"))
    {
        const auto tail = text.substring (split);
        const auto tailWidth = font.getStringWidthFloat (tail);
        if (tailWidth < width * 0.6f)
            if (const auto head = ellipsizedText (text.substring (0, split), font, width - tailWidth); head.isNotEmpty())
                return head + tail;
    }
    return ellipsizedText (text, font, width);
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

// Keep a Japanese stem and its following kana together. Script changes after kana give
// natural phrase boundaries ("音源の / 登録情報を / 確かめます"). Fixed UI terms may
// contain spaces or several scripts; they remain one piece too.
juce::StringArray breakUnits (const juce::String& paragraph)
{
    static const juce::StringArray terms {
        juce::String::fromUTF8 (u8"Reference のセット"), juce::String::fromUTF8 (u8"Reference の範囲"),
        juce::String::fromUTF8 (u8"Reference の曲"), juce::String::fromUTF8 (u8"CHECK セット"),
        "Kirin OS", "Check Preset", "Blind Compare", "Version Blind", "Meter Session",
        "True Peak", "Sample Peak", "Peak Hold", "Momentary Loudness", "Short-term Loudness",
        "Loudness Range", "Buffer size", juce::String::fromUTF8 (u8"Stereo と位相"),
        "SOURCE 1", "SOURCE 2", "PRE / POST", "PRE/POST",
        juce::String::fromUTF8 (u8"選び直"), juce::String::fromUTF8 (u8"押し直"),
        juce::String::fromUTF8 (u8"やり直"), juce::String::fromUTF8 (u8"準備し直"),
        juce::String::fromUTF8 (u8"保存し直"), juce::String::fromUTF8 (u8"作り直"),
        juce::String::fromUTF8 (u8"読み込"), juce::String::fromUTF8 (u8"読み取"),
        juce::String::fromUTF8 (u8"取り込"), juce::String::fromUTF8 (u8"取り消"),
        juce::String::fromUTF8 (u8"書き込"), juce::String::fromUTF8 (u8"書き出"),
        juce::String::fromUTF8 (u8"書き直"), juce::String::fromUTF8 (u8"呼び出"),
        juce::String::fromUTF8 (u8"切り替"), juce::String::fromUTF8 (u8"置き換"),
        juce::String::fromUTF8 (u8"繰り返"), juce::String::fromUTF8 (u8"立ち上"),
        juce::String::fromUTF8 (u8"聴き比"), juce::String::fromUTF8 (u8"聴き取"),
        juce::String::fromUTF8 (u8"聞き取"), juce::String::fromUTF8 (u8"差し引")
    };
    const auto script = [] (juce::juce_wchar c) {
        if (c >= 0x3040 && c <= 0x309f) return 1;
        if (c >= 0x30a0 && c <= 0x30ff) return 2;
        if ((c >= 0x3400 && c <= 0x9fff) || c == 0x3005) return 3;
        return 0;
    };
    juce::StringArray units;
    juce::String unit;
    juce::juce_wchar previous = 0;
    for (int index = 0; index < paragraph.length(); ++index)
    {
        const auto rest = paragraph.substring (index);
        const auto term = std::find_if (terms.begin(), terms.end(), [&rest] (const auto& value) {
            return rest.startsWith (value);
        });
        if (term != terms.end())
        {
            if (unit.isNotEmpty() && ! closesNoLine (previous))
            { units.add (unit); unit.clear(); }
            unit += *term;
            index += term->length() - 1;
            previous = term->getLastCharacter();
            continue;
        }
        const auto character = paragraph[index];
        const auto kind = script (character);
        const auto previousKind = script (previous);
        const auto followsStem = previousKind != 0 || juce::CharacterFunctions::isLetterOrDigit (previous)
            || previous == ')' || previous == 0xff09 || previous == 0x300d;
        const auto joins = unit.isNotEmpty()
            && (opensNoLine (character) || closesNoLine (previous)
                || (latinWord (character) && latinWord (previous))
                || (kind != 0 && kind == previousKind)
                || (kind == 1 && followsStem)
                || (kind == 3 && juce::CharacterFunctions::isDigit (previous)));
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
                while (length > 1 && (! fits (line.substring (0, length))
                                      || opensNoLine (line[length]) || closesNoLine (line[length - 1])))
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
            static const auto clauseEnds = juce::String::fromUTF8 (u8"。、！？");
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

float shownWrappedHeight (const juce::String& text, const juce::Font& font, int width)
{
    const auto shown = shownText (text);
    return wrappedHeight (shown, requiresJapaneseGlyphs (shown) ? nativeTextFontLike (font) : font,
                          width);
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
            const juce::Rectangle<float> lineArea (static_cast<float> (area.getX()), top,
                                                   static_cast<float> (area.getWidth()), font.getHeight());
            graphics.drawText (drawn (line, lineArea), lineArea,
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
    graphics.drawMultiLineText (drawn (text, area.toFloat()), area.getX(), baseline, area.getWidth(), justification);
}

template <typename Area>
void drawShown (juce::Graphics& graphics, const juce::String& text, Area area,
                juce::Justification justification, bool useEllipsesIfTooLong)
{
    const auto shown = shownText (text);
    const ShownFont font (graphics, shown);
    graphics.drawText (drawn (shown, area.toFloat()), area, justification, useEllipsesIfTooLong);
}
}

void draw (juce::Graphics& graphics, const juce::String& text,
           juce::Rectangle<int> area, const presentation::Context& context,
           typography::TextRole role, juce::Justification justification,
           int maximumLines, typography::Composition composition, LabelPolicy policy)
{
    if (area.isEmpty() || text.isEmpty())
        return;
    const auto shown = shownText (text, policy);
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
    graphics.drawText (drawn (displayed, area.toFloat()), area, justification, false);
}

void drawEllipsized (juce::Graphics& graphics, const juce::String& text,
                     juce::Rectangle<int> area, juce::Justification justification)
{
    if (area.isEmpty() || text.isEmpty()) return;
    const auto shown = shownText (text);
    const ShownFont font (graphics, shown);
    graphics.drawText (drawn (ellipsizedText (shown, graphics.getCurrentFont(),
                                              static_cast<float> (area.getWidth())), area.toFloat()),
                       area, justification, false);
}

void drawEllipsizedKeepingVersion (juce::Graphics& graphics, const juce::String& text,
                                   juce::Rectangle<int> area, juce::Justification justification)
{
    if (area.isEmpty() || text.isEmpty()) return;
    const auto shown = shownText (text);
    const ShownFont font (graphics, shown);
    graphics.drawText (drawn (ellipsizedKeepingVersion (shown, graphics.getCurrentFont(),
                                                        static_cast<float> (area.getWidth())), area.toFloat()),
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
        graphics.drawText (drawn (shown, area.toFloat()), area, justification, true);
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
