#pragma once

#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <array>
#include <utility>

namespace hypha::blind_ui::result_layout
{
// The active RESULT inherits the same two independent causes as the trial.
// The frozen PNG has its own already-localized notes and never calls this.
inline juce::String liveNotes (const juce::String& instruction, const juce::String& endExplanation,
                              const juce::String& notice)
{
    juce::StringArray lines;
    for (const auto& line : { endExplanation, notice })
        if (line.isNotEmpty()) lines.add (text_style::shownText (line));
    if (instruction.isNotEmpty() && ! notice.contains (instruction))
        lines.addIfNotAlreadyThere (text_style::shownText (instruction));
    return lines.joinIntoString ("\n");
}
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
