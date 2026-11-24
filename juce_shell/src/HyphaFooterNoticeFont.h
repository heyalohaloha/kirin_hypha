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
    const bool readable = text == "Cannot write to folder. Check access." || text == "2MIX has no DRUM; showing TIME / HISTORY"
        || text == live_compare_ui::keepTailEndedNotice || text == record_ui::wavWaiting
        || text == record_ui::wavWaitingShort || text == live_compare_ui::preProcessingNotice
        || text.startsWith ("Waiting up to 10 s, then ")
        || text == comparison_ui::beforeRealtimeBounce || text == comparison_ui::endThenRetry;
    // Compared in the size it would be drawn in: a notice that only just fits is not drawn past its room.
    const auto larger = monoFont (context, readable ? typography::TextRole::noticeBody : typography::TextRole::action);
    const bool compact = text_style::shownWidth (larger, text) > static_cast<float> (availableWidth);
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

// A sentence with a short form is never cut: the short form is drawn where the whole one has no room.
inline juce::String textFor (const presentation::Context& context, const juce::String& notice, juce::Rectangle<int> room)
{
    const bool whole = notice != record_ui::wavWaiting
        || text_style::shownWidth (fontForText (context, notice, room.getWidth()), notice) <= room.getWidth()
        || wrappedHeight (context, notice, room.getWidth()) <= room.getHeight();
    return whole ? notice : juce::String (record_ui::wavWaitingShort);
}

// The row a notice needs to be read on one line: its short form where it has one (0 without a notice).
inline int leastWidth (const presentation::Context& context, const juce::String& notice)
{
    if (notice.isEmpty()) return 0;
    const juce::String least = notice == record_ui::wavWaiting ? juce::String (record_ui::wavWaitingShort) : notice;
    return juce::roundToInt (std::ceil (text_style::shownWidth (fontForText (context, least, 1), least))) + 10;
}

inline void draw (juce::Graphics& g, const presentation::Context& context,
                  const juce::String& notice, juce::Rectangle<int> room)
{
    const auto text = textFor (context, notice, room);
    g.setFont (monoFont (context, roleForText (context, text, room.getWidth())));
    if (text_style::shownWidth (g.getCurrentFont(), text) > room.getWidth()
        && wrappedHeight (context, text, room.getWidth()) <= room.getHeight())
        text_style::drawLines (g, text, room, juce::Justification::centredLeft, 2);
    else
        text_style::drawEllipsized (g, text, room, juce::Justification::centredLeft);
}
}
