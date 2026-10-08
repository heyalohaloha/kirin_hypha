#pragma once

#include "HyphaTextStyle.h"

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
}
