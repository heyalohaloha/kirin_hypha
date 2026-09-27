#pragma once

#include "../src/HyphaTimeHistoryPainter.h"

namespace hypha::tests
{
inline void profileTimeHistoryPaint (const std::vector<KirinMeterHistoryEntry>& history)
{
    if (juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_RENDER_PROFILE", {}) != "1") return;
    observatory::View view (observatory::Role::post);
    view.setSize (600, 400);
    view.setDomain (observatory::Domain::time);
    view.setHistory (history);
    juce::Image image (juce::Image::ARGB, 600, 400, true);
    juce::Graphics graphics (image);
    observatory_world::Backdrop backdrop;
    observatory_world::State state;
    state.domain = observatory::Domain::time;
    state.density = observatory::Density::observatory;
    auto graph = view.bodyBounds();
    graph.removeFromTop (view.timeControlsHeight());
    const auto measure = [] (const char* name, auto paint)
    {
        paint();
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 30; ++i) paint();
        std::cout << "TIME paint stage " << name << '='
                  << (juce::Time::getMillisecondCounterHiRes() - start) / 30 << " ms\n";
    };
    measure ("backdrop", [&] { backdrop.draw (graphics, image.getBounds(), state); });
    measure ("specimen", [&] { backdrop.drawHyphaSpecimen (graphics, view.bodyBounds(), state); });
    measure ("domain", [&] { observatory_world::paintDomainBed (graphics, view.bodyBounds(), state); });
    measure ("graph", [&] { time_history::paint (graphics, graph, history, "", false, false,
                                                meter_context::ScaleMode::wide,
                                                presentation::forEditor (900, 600)); });
    measure ("shell", [&] { view.paint (graphics); });
    measure ("children", [&] { view.paintEntireComponent (graphics, true); });
}
}
