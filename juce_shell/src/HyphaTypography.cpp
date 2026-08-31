#include "HyphaTheme.h"

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

juce::Font labelFont (float height)
{
    if (const auto typeface = kimeraTypeface())
        return juce::Font (typeface).withHeight (height);
    return fallbackFont (nativeFallbackLabelFontFamily(), height);
}

juce::Font monoFont (float height)
{
    if (const auto typeface = kimeraTypeface())
        return juce::Font (typeface).withHeight (height);
    return fallbackFont (nativeFallbackMonoFontFamily(), height);
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
