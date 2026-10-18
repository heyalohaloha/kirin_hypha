#pragma once

#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

// 2026-10-18: the saved measurement image is checked at the size it is written. The axis
// numbers of the LEVEL history, its TP overlay and the TP meter never overlap one another (a short
// axis names fewer marks), and the footer keeps the space between the state and the seconds.
namespace hypha::tests
{
inline void verifyCaptureLabelSpacing (observatory::View& post, const std::vector<KirinMeterHistoryEntry>& history)
{
    const auto require = [] (bool valid, const juce::String& message)
    {
        if (valid) return;
        std::cerr << "Capture label spacing: " << message << '\n';
        std::exit (EXIT_FAILURE);
    };
    const auto isAxisNumber = [] (const juce::String& text)
    {
        const auto body = text.trimCharactersAtStart ("+-");
        return body.isNotEmpty() && body.containsOnly ("0123456789");
    };
    post.setDomain (observatory::Domain::level);
    for (const auto size : { std::pair { 1'200, 630 }, std::pair { 1'080, 1'080 }, std::pair { 1'080, 1'350 } })
    {
        text_style::ShownTextLog log;
        post.createCaptureImage (size.first, size.second, false, "2026-09-01 00:00:00", "0.1.0", {}, &history);
        const auto where = juce::String (size.first) + "x" + juce::String (size.second);
        // Axis numbers drawn in the same column, in order down the axis.
        std::map<std::pair<int, int>, std::vector<juce::Rectangle<float>>> columns;
        for (int index = 0; index < log.texts().size(); ++index)
            if (isAxisNumber (log.texts()[index]) && ! log.areas()[(size_t) index].isEmpty())
            {
                const auto area = log.areas()[(size_t) index];
                columns[{ juce::roundToInt (area.getX()), juce::roundToInt (area.getWidth()) }].push_back (area);
            }
        require (! columns.empty(), "the capture draws axis numbers: " + where);
        for (auto& [column, areas] : columns)
        {
            std::sort (areas.begin(), areas.end(), [] (const auto& a, const auto& b) { return a.getY() < b.getY(); });
            for (size_t index = 1; index < areas.size(); ++index)
                require (areas[index].getY() >= areas[index - 1].getBottom(),
                         "axis numbers do not overlap in " + where + " at x " + juce::String (column.first)
                             + ": " + areas[index - 1].toString() + " / " + areas[index].toString());
        }
        bool footer = false;
        for (const auto& text : log.texts())
            if (text.contains ("ITU-R BS.1770"))
            {
                footer = true;
                const auto status = text.upToFirstOccurrenceOf (" S  |", false, false);
                const auto word = status.upToLastOccurrenceOf (" ", false, false);
                require (status.contains (" ") && word.isNotEmpty() && word.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZ "),
                         "the footer keeps a space between the state and the seconds: " + text);
            }
        require (footer, "the footer names the measurement standard: " + where);
    }
}
}
