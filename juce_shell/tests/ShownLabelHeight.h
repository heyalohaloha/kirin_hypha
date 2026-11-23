#pragma once
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaTheme.h"

namespace hypha::tests
{
// The height the text of a label needs at the label's own width, by the rule that paints it.
// Japanese is broken into lines by text_style and painted line by line in the native Japanese
// font, so its height is those lines. Laying it out with the label's Latin font measures the font
// the system substitutes for the missing glyphs instead, and that font follows the system language:
// the same label then needs a different height on a Japanese and on an English system, and neither
// is the height of what is painted. Other text keeps the wrapping JUCE paints it with.
inline float shownLabelHeight (const juce::Label& label)
{
    const auto room = label.getBorderSize().subtractedFrom (label.getLocalBounds());
    const auto shown = text_style::shownText (label.getText());
    const auto painted = text_style::shownWrappedHeight (label.getText(), label.getFont(), room.getWidth());
    if (requiresJapaneseGlyphs (shown))
        return painted;
    juce::AttributedString text;
    text.append (shown, label.getFont(), juce::Colours::white);
    juce::TextLayout layout;
    layout.createLayout (text, static_cast<float> (room.getWidth()));
    return juce::jmax (layout.getHeight(), painted);
}
}
