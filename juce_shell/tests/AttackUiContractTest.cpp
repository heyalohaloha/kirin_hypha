#include "../src/HyphaAttackInternalComponent.h"
#include "../src/HyphaAttackUiContract.h"
#include "../src/HyphaSpectrumUiContract.h"
#include "../src/HyphaTheme.h"

#include <cstdlib>
#include <iostream>

namespace
{
    void require (bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "ATTACK UI contract failed at line " << line
                  << ": " << expression << '\n';
        std::exit (EXIT_FAILURE);
    }

#define KIRIN_REQUIRE(expression) require ((expression), #expression, __LINE__)

    bool nearRgb (juce::Colour pixel, juce::Colour target)
    {
        constexpr int tolerance = 12;
        return pixel.getAlpha() > 16
            && std::abs ((int) pixel.getRed() - (int) target.getRed()) <= tolerance
            && std::abs ((int) pixel.getGreen() - (int) target.getGreen()) <= tolerance
            && std::abs ((int) pixel.getBlue() - (int) target.getBlue()) <= tolerance;
    }

    int countRuns (const juce::Image& image, juce::Rectangle<int> requested)
    {
        const auto area = requested.getIntersection (image.getBounds());
        int runs = 0;
        bool previousColumn = false;
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            bool currentColumn = false;
            for (int y = area.getY(); y < area.getBottom(); ++y)
                currentColumn = currentColumn
                    || nearRgb (image.getPixelAt (x, y), hypha::COL_SPECTRUM_DELTA_BR);
            if (currentColumn && ! previousColumn)
                ++runs;
            previousColumn = currentColumn;
        }
        return runs;
    }

    juce::Image render (hypha::AttackInternalComponent& component)
    {
        juce::Image image (
            juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
        juce::Graphics graphics (image);
        component.paintEntireComponent (graphics, true);
        return image;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    hypha::AttackInternalComponent component;
    const auto bounds = hypha::ui_contract::spectrumPlotBounds();
    component.setSize (bounds.width, bounds.height);

    KirinAttackStats stats {};
    stats.available = 1;
    stats.enabled = 1;
    stats.worker_running = 1;
    KirinAttackEventBatch events {};
    events.capacity = KIRIN_ATTACK_EVENT_BATCH_CAPACITY;
    events.count = 4;
    for (std::uint32_t index = 0; index < events.count; ++index)
    {
        events.events[index].generation = 7;
        events.events[index].sample_rate = 48'000;
    }
    events.events[0].event_sample = 0;
    events.events[1].event_sample = 144'000;
    events.events[2].event_sample = 288'000;
    events.events[3].event_sample = 200'000;
    events.events[3].generation = 6; // stale transport generation must not be painted

    component.setSnapshot (events, 288'000, 48'000, 7, stats);
    const auto image = render (component);
    KIRIN_REQUIRE (countRuns (image, { 0, 30, image.getWidth(), 36 }) == 3);

    stats.worker_running = 0;
    component.setSnapshot (events, 288'000, 48'000, 7, stats);
    const auto warming = render (component);
    KIRIN_REQUIRE (countRuns (warming, { 0, 30, warming.getWidth(), 36 }) == 0);
    std::cout << "ATTACK UI contract passed: three confirmed positions, stale generation hidden\n";
    return EXIT_SUCCESS;
}
