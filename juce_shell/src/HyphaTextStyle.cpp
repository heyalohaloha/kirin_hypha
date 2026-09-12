#include "HyphaTextStyle.h"

namespace hypha::text_style
{
int requiredWidth (const juce::Font& font, const juce::String& text,
                   const typography::TextStyle& style, int minimum)
{
    return juce::jmax (minimum, juce::roundToInt (std::ceil (
        font.getStringWidthFloat (text) + 2.0f * style.horizontalPadding)));
}

int requiredLineHeight (const typography::TextStyle& style, int minimum) noexcept
{
    return juce::jmax (minimum, juce::roundToInt (std::ceil (style.lineHeight)));
}

namespace
{
juce::String ellipsize (const juce::String& text, const juce::Font& font, float width)
{
    if (font.getStringWidthFloat (text) <= width)
        return text;
    const auto marker = juce::String::charToString (0x2026);
    if (font.getStringWidthFloat (marker) > width)
        return {};
    for (auto length = text.length() - 1; length > 0; --length)
    {
        const auto candidate = text.substring (0, length).trimEnd() + marker;
        if (font.getStringWidthFloat (candidate) <= width)
            return candidate;
    }
    return marker;
}

void drawWrapped (juce::Graphics& graphics, const juce::String& text,
                  juce::Rectangle<int> area, juce::Justification justification)
{
    const auto font = graphics.getCurrentFont();
    juce::AttributedString attributed;
    attributed.append (text, font, juce::Colours::white);
    juce::TextLayout layout;
    layout.createLayout (attributed, static_cast<float> (area.getWidth()));
    const auto baseline = area.getY()
        + juce::roundToInt (juce::jmax (0.0f,
              (static_cast<float> (area.getHeight()) - layout.getHeight()) * 0.5f)
              + font.getAscent());
    const auto startX = justification.testFlags (juce::Justification::horizontallyCentred)
        ? area.getCentreX() : justification.testFlags (juce::Justification::right)
            ? area.getRight() : area.getX();
    juce::Graphics::ScopedSaveState saved (graphics);
    graphics.reduceClipRegion (area);
    graphics.drawMultiLineText (text, startX, baseline, area.getWidth(), justification);
}
}

void draw (juce::Graphics& graphics, const juce::String& text,
           juce::Rectangle<int> area, const presentation::Context& context,
           typography::TextRole role, juce::Justification justification,
           int maximumLines, typography::Composition composition)
{
    if (area.isEmpty() || text.isEmpty())
        return;
    const auto overflow = typography::resolve (context, role, composition).overflow;
    if (overflow == typography::Overflow::wrap && maximumLines > 1)
    {
        drawWrapped (graphics, text, area, justification);
        return;
    }
    const auto displayed = overflow == typography::Overflow::ellipsize
        ? ellipsize (text, graphics.getCurrentFont(), static_cast<float> (area.getWidth()))
        : text;
    graphics.drawText (displayed, area, justification, false);
}

void drawEllipsized (juce::Graphics& graphics, const juce::String& text,
                     juce::Rectangle<int> area, juce::Justification justification)
{
    graphics.drawText (ellipsize (text, graphics.getCurrentFont(),
                                  static_cast<float> (area.getWidth())),
                       area, justification, false);
}
}
