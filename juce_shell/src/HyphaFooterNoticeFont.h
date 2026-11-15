#pragma once

#include "HyphaTextStyle.h"
#include "HyphaRecordBindingNotice.h"
#include "HyphaPreProcessingNotice.h"

namespace hypha::footer_notice_font
{
// Only a sentence that exceeds its existing slot uses the smaller, established legend size.
// Short notices and the footer's LIVE / HOLD / CHAIN LOAD keep the buttons' action size.
inline typography::TextRole roleForText (const presentation::Context& context,
                                        const juce::String& text, int availableWidth)
{
    const auto action = monoFont (context, typography::TextRole::action);
    return text_style::shownWidth (action, text) > static_cast<float> (availableWidth)
        ? typography::TextRole::legend : typography::TextRole::action;
}

inline juce::Font fontForText (const presentation::Context& context, const juce::String& text, int width)
{
    auto font = monoFont (context, roleForText (context, text, width));
    if (text == "Cannot write to folder. Check access." || text == "2MIX has no DRUM; showing TIME / HISTORY"
        || text == record_ui::wavWaiting || text == live_compare_ui::preProcessingNotice)
        font.setHeight (juce::jmax (14.0f, font.getHeight()));
    return font;
}

// Layout and paint share the same localized font and wrapping rules. Only a slot that has
// actually reserved the full height wraps; other surfaces keep their established single line.
inline int wrappedHeight (const presentation::Context& context, const juce::String& text, int width)
{
    const auto font = fontForText (context, text, width);
    return juce::roundToInt (std::ceil (text_style::shownWrappedHeight (text, font, juce::jmax (1, width))));
}

inline void draw (juce::Graphics& g, const presentation::Context& context,
                  const juce::String& text, juce::Rectangle<int> room)
{
    g.setFont (fontForText (context, text, room.getWidth()));
    if (text_style::shownWidth (g.getCurrentFont(), text) > room.getWidth()
        && wrappedHeight (context, text, room.getWidth()) <= room.getHeight())
        text_style::drawLines (g, text, room, juce::Justification::centredLeft, 2);
    else
        text_style::drawEllipsized (g, text, room, juce::Justification::centredLeft);
}
}
