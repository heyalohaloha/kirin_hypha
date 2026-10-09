#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#if KIRIN_HYPHA_KIMERA_EMBEDDED
 #include "BinaryData.h"
#endif

namespace hypha
{
namespace
{
juce::Typeface::Ptr embeddedKimeraTypeface()
{
#if KIRIN_HYPHA_KIMERA_EMBEDDED
    static const auto typeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::KMRWaldenburgBook_otf,
        static_cast<size_t> (BinaryData::KMRWaldenburgBook_otfSize));
    return typeface;
#else
    return {};
#endif
}

juce::Typeface::Ptr installedKimeraTypeface()
{
    static const auto typeface = []
    {
        const auto names = juce::Font::findAllTypefaceNames();
        if (! names.contains (ui_contract::kimeraFontFamily, true))
            return juce::Typeface::Ptr {};
        const juce::Font request (ui_contract::kimeraFontFamily, 16.0f, juce::Font::plain);
        const auto resolved = juce::Typeface::createSystemTypefaceFor (request);
        return resolved != nullptr && resolved->getName().containsIgnoreCase ("Waldenburg")
             ? resolved : juce::Typeface::Ptr {};
    }();
    return typeface;
}

juce::Typeface::Ptr kimeraTypeface()
{
    if (const auto embedded = embeddedKimeraTypeface())
        return embedded;
    return installedKimeraTypeface();
}

juce::Font fallbackFont (const char* family, float height)
{
    return juce::Font (family, height, juce::Font::plain);
}

juce::String nativeTextFontFamily()
{
    static const auto family = []
    {
        const auto installed = juce::Font::findAllTypefaceNames();
#if JUCE_WINDOWS
        const std::array candidates { "Yu Gothic UI", "Meiryo UI", "Segoe UI" };
#elif JUCE_MAC
        const std::array candidates { ".Hiragino Kaku Gothic Interface", "Hiragino Sans" };
#else
        const std::array candidates { "Noto Sans CJK JP", "Noto Sans", "DejaVu Sans" };
#endif
        for (const auto* candidate : candidates)
            if (installed.contains (candidate, true))
                return juce::String { candidate };
        return juce::Font::getDefaultSansSerifFontName();
    }();
    return family;
}

juce::Font makeLabelFont (float height)
{
    height = juce::jmax (11.0f, height);
    if (const auto typeface = kimeraTypeface())
        return juce::Font (typeface).withHeight (height);
    return fallbackFont (nativeFallbackLabelFontFamily(), height);
}

juce::Font makeMonoFont (float height)
{
    height = juce::jmax (11.0f, height);
    if (const auto typeface = kimeraTypeface())
        return juce::Font (typeface).withHeight (height);
    return fallbackFont (nativeFallbackMonoFontFamily(), height);
}

juce::Font makeNativeTextFont (float height)
{
    return juce::Font (nativeTextFontFamily(), juce::jmax (11.0f, height),
                       juce::Font::plain);
}
}

const char* nativeFallbackLabelFontFamily() noexcept
{
#if JUCE_WINDOWS
    return ui_contract::windowsFallbackLabelFontFamily;
#else
    return ui_contract::fallbackLabelFontFamily;
#endif
}

const char* nativeFallbackMonoFontFamily() noexcept
{
#if JUCE_WINDOWS
    return ui_contract::windowsFallbackMonoFontFamily;
#else
    return ui_contract::fallbackMonoFontFamily;
#endif
}

bool usingKimeraTypography() noexcept
{
    return kimeraTypeface() != nullptr;
}

bool requiresNativeTextFont (const juce::String& text) noexcept
{
    for (auto cursor = text.getCharPointer(); ! cursor.isEmpty(); ++cursor)
        if (*cursor > static_cast<juce::juce_wchar> (0x024f))
            return true;
    return false;
}

bool requiresJapaneseGlyphs (const juce::String& text) noexcept
{
    for (auto cursor = text.getCharPointer(); ! cursor.isEmpty(); ++cursor)
    {
        const auto character = *cursor;
        if ((character >= 0x3000 && character <= 0x30ff)      // CJK punctuation, kana
            || (character >= 0x31f0 && character <= 0x31ff)   // katakana extensions
            || (character >= 0x3400 && character <= 0x4dbf)   // CJK extension A
            || (character >= 0x4e00 && character <= 0x9fff)   // CJK unified ideographs
            || (character >= 0xff00 && character <= 0xffef))  // full-width and half-width forms
            return true;
    }
    return false;
}

juce::Font nativeTextFontLike (const juce::Font& contracted)
{
    return makeNativeTextFont (contracted.getHeight());
}

juce::Font labelFont (const presentation::Context& context,
                      typography::TextRole role,
                      typography::Composition composition)
{
    return makeLabelFont (typography::resolve (context, role, composition).fontHeight);
}

juce::Font monoFont (const presentation::Context& context,
                     typography::TextRole role,
                     typography::Composition composition)
{
    return makeMonoFont (typography::resolve (context, role, composition).fontHeight);
}

juce::Font nativeTextFont (const presentation::Context& context,
                           typography::TextRole role,
                           typography::Composition composition)
{
    return makeNativeTextFont (typography::resolve (context, role, composition).fontHeight);
}

juce::Font displayTextFont (const juce::String& text,
                            const presentation::Context& context,
                            typography::TextRole role,
                            typography::Composition composition)
{
    return requiresNativeTextFont (text) ? nativeTextFont (context, role, composition)
                                         : labelFont (context, role, composition);
}

juce::Font displayTextFont (const juce::String& text,
                            const presentation::Context& context,
                            typography::TextRole role,
                            typography::Composition composition,
                            juce::Rectangle<float> area, bool tabular)
{
    const auto font = displayTextFont (text, context, role, composition);
    const auto width = tabular ? tabularTextWidth (font, text)
                               : text_style::shownWidth (font, text, text_style::LabelPolicy::fixed);
    const auto ratio = juce::jmin (1.0f, area.getWidth() / juce::jmax (1.0f, width),
                                 area.getHeight() / juce::jmax (1.0f, font.getHeight()));
    return font.withHeight (font.getHeight() * juce::jmax (0.01f, ratio));
}

float tabularTextWidth (const juce::Font& font, const juce::String& text)
{
    float digitCell = 0.0f;
    for (juce::juce_wchar digit = '0'; digit <= '9'; ++digit)
        digitCell = juce::jmax (digitCell,
            font.getStringWidthFloat (juce::String::charToString (digit)));

    float width = 0.0f;
    for (auto cursor = text.getCharPointer(); ! cursor.isEmpty(); ++cursor)
    {
        const auto character = *cursor;
        width += character >= '0' && character <= '9'
               ? digitCell
               : font.getStringWidthFloat (juce::String::charToString (character));
    }
    return width;
}

void drawTabularText (juce::Graphics& graphics,
                      const juce::Font& font,
                      const juce::String& text,
                      juce::Rectangle<float> area,
                      juce::Justification justification)
{
    const auto width = tabularTextWidth (font, text);
    float x = area.getX();
    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x += (area.getWidth() - width) * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x += area.getWidth() - width;

    float digitCell = 0.0f;
    for (juce::juce_wchar digit = '0'; digit <= '9'; ++digit)
        digitCell = juce::jmax (digitCell,
            font.getStringWidthFloat (juce::String::charToString (digit)));

    graphics.setFont (font);
    const juce::Justification cellJustification (
        juce::Justification::horizontallyCentred | justification.getOnlyVerticalFlags());
    for (auto cursor = text.getCharPointer(); ! cursor.isEmpty(); ++cursor)
    {
        const auto character = *cursor;
        const auto glyph = juce::String::charToString (character);
        const auto advance = character >= '0' && character <= '9'
                           ? digitCell : font.getStringWidthFloat (glyph);
        graphics.drawText (glyph,
                           juce::Rectangle<float> { x, area.getY(), advance, area.getHeight() },
                           cellJustification, false);
        x += advance;
    }
}
}
