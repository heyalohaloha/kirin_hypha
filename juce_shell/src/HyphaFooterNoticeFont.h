#pragma once

#include "HyphaTextStyle.h"
#include "HyphaRecordBindingNotice.h"
#include "HyphaPreProcessingNotice.h"
#include "HyphaComparisonNotices.h"
#include "HyphaQueuedStartText.h"

namespace hypha::footer_notice_font
{
// Only a sentence that exceeds its existing slot uses the smaller, established legend size.
// Short notices and the footer's LIVE / HOLD / CHAIN LOAD keep the buttons' action size.
inline typography::TextRole roleForText (const presentation::Context& context,
                                        const juce::String& text, int availableWidth)
{
    const auto action = monoFont (context, typography::TextRole::action);
    const bool compact = text_style::shownWidth (action, text) > static_cast<float> (availableWidth);
    const bool readable = text == "Cannot write to folder. Check access." || text == "2MIX has no DRUM; showing TIME / HISTORY"
        || text == live_compare_ui::keepTailEndedNotice || text == record_ui::wavWaiting || text == live_compare_ui::preProcessingNotice
        || text.startsWith ("Waiting up to 10 s, then ")
        || text == comparison_ui::beforeRealtimeBounce || text == comparison_ui::endThenRetry;
    if (readable) return compact ? typography::TextRole::noticeCompact : typography::TextRole::noticeBody;
    return compact ? typography::TextRole::legend : typography::TextRole::action;
}

inline juce::Font fontForText (const presentation::Context& context, const juce::String& text, int width)
{
    return monoFont (context, roleForText (context, text, width));
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
    g.setFont (monoFont (context, roleForText (context, text, room.getWidth())));
    if (text_style::shownWidth (g.getCurrentFont(), text) > room.getWidth()
        && wrappedHeight (context, text, room.getWidth()) <= room.getHeight())
        text_style::drawLines (g, text, room, juce::Justification::centredLeft, 2);
    else
        text_style::drawEllipsized (g, text, room, juce::Justification::centredLeft);
}
}
