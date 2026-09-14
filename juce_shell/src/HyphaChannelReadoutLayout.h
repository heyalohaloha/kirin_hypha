#pragma once
#include "HyphaTheme.h"
#include <cmath>

namespace hypha::observatory
{
// The numeric header uses the whole strip width, independent of the narrower L/R bars.
// Reserve six characters for finite float input peaks, including a negative three-digit value.
inline int channelStripWidth (presentation::Context context, int minimum)
{
    const auto font = monoFont (context, typography::TextRole::secondaryValue,
                               typography::Composition::instrument);
    return juce::jmax (minimum, (int) std::ceil (tabularTextWidth (font, "-999.9")) + 36);
}

inline juce::Rectangle<int> channelPeakValueArea (juce::Rectangle<int> row)
{
    row.removeFromLeft (20);
    return row;
}
}
