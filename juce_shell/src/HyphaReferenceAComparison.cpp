#include "HyphaReferenceAComparison.h"

#include "HyphaTextStyle.h"

#include <cmath>

namespace hypha::reference_ui
{
namespace
{
constexpr const char* more[] { "MORE", "HIGHER", "LARGER", "LOUDER", "WIDER" };
constexpr const char* less[] { "LESS", "LOWER", "SMALLER", "QUIETER", "NARROWER" };

// 画面の言語の文を、数字の前・数字・数字の後ろに分ける（日本語は「Aが」「3.7」「少ない」）。
struct Pieces
{
    juce::String before, number, after;
};

Pieces split (const AComparison& comparison)
{
    const auto shown = text_style::shownText (comparison.text);
    const auto at = comparison.number.isEmpty() ? -1 : shown.indexOf (comparison.number);
    if (at < 0) return { shown, {}, {} };
    return { shown.substring (0, at), comparison.number, shown.substring (at + comparison.number.length()) };
}

// 描いたときの幅とベースラインまでの高さ。日本語は字を替えて描くので、替えた字で測る（HyphaTextStyle.cpp と同じ）。
struct Extent
{
    float width = 0.0f, ascent = 0.0f, height = 0.0f;
};

Extent extentOf (const juce::Font& font, const juce::String& shown)
{
    const auto drawn = requiresJapaneseGlyphs (shown) ? nativeTextFontLike (font) : font;
    return { shown.isEmpty() ? 0.0f : drawn.getStringWidthFloat (shown), drawn.getAscent(), drawn.getHeight() };
}

juce::Font wordsFont (const presentation::Context& context)
{
    return labelFont (context, typography::TextRole::unit, typography::Composition::information);
}

juce::Font numberFont (const presentation::Context& context)
{
    return monoFont (context, typography::TextRole::readout, typography::Composition::information);
}
}

AComparison compareA (double aMinusOther, int decimals, const juce::String& unit, AWords words, char other)
{
    if (! std::isfinite (aMinusOther)) return {};
    const auto scale = std::pow (10.0, decimals);
    const auto magnitude = std::round (std::abs (aMinusOther) * scale) / scale;
    if (magnitude < 0.5 / scale)
        return { "A SAME AS " + juce::String::charToString (static_cast<juce::juce_wchar> (other)), {}, {} };
    const auto number = juce::String (magnitude, decimals);
    const auto index = static_cast<size_t> (words);
    const juce::String word { aMinusOther > 0.0 ? more[index] : less[index] };
    return { "A " + number + unit + " " + word, number, word };
}

AComparison compareBand (double aMinusOther)
{
    if (! std::isfinite (aMinusOther)) return {};
    const auto magnitude = std::round (std::abs (aMinusOther) * 10.0) / 10.0;
    if (magnitude < 0.05) return { "SAME", {}, {} };
    const auto number = juce::String (magnitude, 1);
    const juce::String word { aMinusOther > 0.0 ? more[0] : less[0] };
    return { number + " " + word, number, word };
}

float aComparisonWidth (const AComparison& comparison, const presentation::Context& context)
{
    if (! comparison.shown()) return 0.0f;
    const auto pieces = split (comparison);
    const auto words = wordsFont (context);
    return extentOf (words, pieces.before).width + extentOf (numberFont (context), pieces.number).width
        + extentOf (words, pieces.after).width;
}

void paintAComparison (juce::Graphics& g, const AComparison& comparison, juce::Rectangle<float> area, juce::Justification justification,
                       const presentation::Context& context, juce::Colour wordsColour, juce::Colour numberColour)
{
    if (! comparison.shown() || area.isEmpty()) return;
    const auto width = aComparisonWidth (comparison, context);
    if (width > area.getWidth())  // 詰めずに省略する
    {
        g.setColour (wordsColour);
        g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
        text_style::drawEllipsized (g, comparison.text, area.toNearestInt(), justification);
        return;
    }
    const auto pieces = split (comparison);
    const auto digits = extentOf (numberFont (context), pieces.number);
    const auto before = extentOf (wordsFont (context), pieces.before), after = extentOf (wordsFont (context), pieces.after);
    // 言葉と数字のベースラインをそろえる（数字の字を縦の中央に置く。数字が無ければ言葉を）。
    const auto main = pieces.number.isNotEmpty() ? digits : before;
    const auto baseline = area.getCentreY() - main.height * 0.5f + main.ascent;
    auto x = justification.testFlags (juce::Justification::right) ? area.getRight() - width
           : justification.testFlags (juce::Justification::horizontallyCentred) ? area.getCentreX() - width * 0.5f
           : area.getX();
    const auto piece = [&g, &x, baseline] (const juce::String& text, const Extent& extent)
    {
        if (text.isEmpty()) return;
        text_style::drawText (g, text, juce::Rectangle<float> (x, baseline - extent.ascent, extent.width + 1.0f, extent.height),
                              juce::Justification::centredLeft, false);
        x += extent.width;
    };
    g.setColour (wordsColour);
    g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
    piece (pieces.before, before);
    g.setColour (numberColour);
    g.setFont (monoFont (context, typography::TextRole::readout, typography::Composition::information));
    piece (pieces.number, digits);
    g.setColour (wordsColour);
    g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
    piece (pieces.after, after);
}
}
