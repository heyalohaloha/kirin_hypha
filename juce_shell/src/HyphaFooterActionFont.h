#pragma once

#include "HyphaTheme.h"

namespace hypha::footer_action_font
{
inline bool isEnd (const juce::String& text) { return text == "END" || text.startsWith ("END +"); }
inline juce::Font forText (const presentation::Context& context, const juce::String& text)
{
    return labelFont (context, isEnd (text) ? typography::TextRole::listenAction : typography::TextRole::action);
}
}
