#pragma once

#include "HyphaTheme.h"

namespace hypha::footer_action_font
{
inline bool isEnd (const juce::String& text) { return text == "END" || text.startsWith ("END +"); }
inline juce::Font forText (const presentation::Context& context, const juce::String& text)
{
    auto font = labelFont (context, typography::TextRole::action);
    if (isEnd (text)) font.setHeight (juce::jmax (12.0f, font.getHeight()));
    return font;
}
}
