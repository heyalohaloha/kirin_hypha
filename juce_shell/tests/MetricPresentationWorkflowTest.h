#pragma once
#include "../src/HyphaChannelReadoutLayout.h"
#include "../src/HyphaObservatoryView.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void verifyMetricPresentationWorkflow()
{
    const auto require = [] (bool ok, const char* message)
    { if (! ok) { std::cerr << "Metric presentation: " << message << '\n'; std::exit (EXIT_FAILURE); } };
    for (const auto preset : observatory::sizePresets)
    {
        observatory::View view (observatory::Role::post);
        view.setSize (preset.width, preset.height);
        juce::Image image (juce::Image::ARGB, preset.width, preset.height, true);
        juce::Graphics graphics (image);
        view.paintEntireComponent (graphics, true);
        bool momentary = false, integrated = false;
        for (int y = 0; y < preset.height; y += 5)
            for (int x = 0; x < preset.width; x += 5)
            {
                const auto help = view.metricHelpAt ({ x, y });
                momentary |= help.contains ("400 ms");
                integrated |= help.contains ("Integrated loudness since");
            }
        require (momentary && integrated, "current window and cumulative values explain their ranges on hover at every size");
        view.setDomain (observatory::Domain::reference);
        require (view.metricHelpAt (view.bodyBounds().getCentre()).isEmpty(),
                 "a page change cannot expose old metric help over Reference");
        if (preset.width < 600) continue;
        const auto context = presentation::forEditor (preset.width, preset.height);
        const auto font = monoFont (context, typography::TextRole::secondaryValue,
                                   typography::Composition::instrument);
        const auto stripWidth = observatory::channelStripWidth (context, preset.width == 900 ? 126 : 116);
        // The actual panel is reduced by 2 px each side, then inset by 5 px.
        const auto numeric = observatory::channelPeakValueArea ({ 0, 0, stripWidth - 14, (int) std::ceil (font.getHeight()) + 2 });
        for (const auto text : { "-14.5", "-114.5", "-897.1", "770.6", "---" })
            require (tabularTextWidth (font, text) <= numeric.getWidth(),
                     "TP retains its existing font and complete signed value inside the numeric area");
    }
}
}
