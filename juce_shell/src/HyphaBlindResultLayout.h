#pragma once

#include "HyphaTheme.h"

#include <array>
#include <utility>

namespace hypha::blind_ui::result_layout
{
// Measure the three result rows at their actual width without shrinking their horizontal metrics.
// The result editor is the first row, followed by compensation and the note.
inline std::pair<int, bool> measureRows (const std::array<juce::TextEditor*, 3>& bodies,
                                         std::array<int, 3>& heights, const presentation::Context& context,
                                         juce::Rectangle<int> bounds, float scale, int padding)
{
    int total = 0;
    bool fitsWidth = true;
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        auto& body = *bodies[i];
        heights[i] = 0;
        if (! body.isVisible() || body.getText().trim().isEmpty()) continue;
        body.setScrollbarsShown (false);
        body.setBounds (bounds.getX(), bounds.getY(), bounds.getWidth(), juce::jmax (1, bounds.getHeight()));
        body.setFont (&body == bodies.front()
            ? nativeTextFont (context, typography::TextRole::primaryValue, typography::Composition::information, scale)
            : displayTextFont (body.getText(), context, typography::TextRole::listenChoice,
                               typography::Composition::information, scale));
        body.applyFontToAllText (body.getFont());
        heights[i] = body.getTextHeight() + padding;
        total += heights[i];
        fitsWidth = fitsWidth && body.getTextWidth() <= bounds.getWidth();
    }
    return std::pair { total, fitsWidth };
}
}
