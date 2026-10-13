#pragma once
#include "FooterNoticeContractChecks.h"

namespace hypha::tests::listen_text
{
inline juce::String compactText (juce::String text) { return text.removeCharacters (" \t\r\n"); }

inline bool labelPaintsCompletely (juce::Label& label)
{
    const auto text = text_style::shownText (label.getText());
    const auto room = label.getBorderSize().subtractedFrom (label.getLocalBounds());
    if (requiresJapaneseGlyphs (text))
    {
        text_style::ShownTextLog actual;
        const auto image = label.createComponentSnapshot (label.getLocalBounds());
        const auto font = nativeTextFontLike (label.getFont());
        for (const auto& line : actual.texts())
            if (font.getStringWidthFloat (line) > room.getWidth() + 0.5f) return false;
        return image.isValid() && compactText (actual.texts().joinIntoString ("")) == compactText (text);
    }
    // JUCE's English Label uses drawFittedText, whose glyph arrangement can add an ellipsis
    // even when a different wrapping estimate fits. Check those exact glyphs as well.
    juce::GlyphArrangement glyphs;
    glyphs.addFittedText (label.getFont(), text, static_cast<float> (room.getX()), static_cast<float> (room.getY()),
        static_cast<float> (room.getWidth()), static_cast<float> (room.getHeight()), label.getJustificationType(),
        juce::jmax (1, static_cast<int> (room.getHeight() / label.getFont().getHeight())), label.getMinimumHorizontalScale());
    juce::String painted;
    for (int i = 0; i < glyphs.getNumGlyphs(); ++i) painted += glyphs.getGlyph (i).getCharacter();
    return compactText (painted) == compactText (text);
}

inline bool previewNotice (const juce::String& text)
{
    return text == "DAW stopped: play to resume (POST)"
        || text == "This DAW has no verified audio clock; PRE is unavailable (POST)"
        || text == "Loop delay unverified; POST; END closes"
        || text == "MATCH held; select PRE to check timing"
        || text == "The capture request could not be sent. Try again after it is released.";
}
}
