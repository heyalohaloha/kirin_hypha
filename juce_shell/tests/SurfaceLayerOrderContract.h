#pragma once

#include "../src/HyphaCaptureHistoryPainter.h"
#include "../src/HyphaRunSummary.h"
#include "../src/HyphaSpacePainter.h"
#include "../src/HyphaSurfaceMaterial.h"
#include "../src/HyphaTimeHistoryPainter.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace hypha::tests
{
namespace surface_layer_order
{
inline void require (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "Surface layer order contract: " << message << '\n';
        std::exit (EXIT_FAILURE);
    }
}

template <typename Paint>
juce::Image render (presentation::Context context, Paint&& paint)
{
    juce::Component root;
    root.setSize (context.logicalWidth, context.logicalHeight);
    const key_light::Scope light (root);
    juce::Image image (juce::Image::ARGB, 600, 400, true);
    juce::Graphics g (image);
    g.fillAll (BG);
    paint (g);
    return image;
}

inline void requireInnerShadow (const juce::Image& framed, const juce::Image& glass,
                                juce::Rectangle<int> window, const char* message)
{
    // A quiet strip below the gold lip and above any axes/text. The frame contributes its own
    // recessed-glass shadow here; painting a panel after that frame erases this exact signal.
    int difference = 0, count = 0;
    for (int y = window.getY() + 2; y <= window.getY() + 4; ++y)
        for (int x = window.getX() + window.getWidth() / 4;
             x < window.getX() + window.getWidth() * 3 / 4; ++x)
        {
            const auto a = framed.getPixelAt (x, y), b = glass.getPixelAt (x, y);
            difference += (int) b.getRed() + (int) b.getGreen() + (int) b.getBlue()
                        - (int) a.getRed() - (int) a.getGreen() - (int) a.getBlue();
            ++count;
        }
    require (count > 0 && difference > count * 2, message);
}

inline juce::Rectangle<int> spaceWindow (juce::Rectangle<int> area, bool compact,
                                        presentation::Context context)
{
    if (compact)
    {
        area.reduce (6, 4);
        const auto column = juce::jlimit (56, 96, (area.getWidth() - area.getHeight()) / 2);
        area.removeFromLeft (column);
        area.removeFromRight (column);
    }
    else
    {
        area.reduce (9, 7);
        area.removeFromTop (18);
        // This fixture is the medium side-by-side density + metrics layout, below MONO's gate.
        area.removeFromRight (juce::jlimit (82, 168, juce::roundToInt (area.getWidth() * 0.31f)));
        area.removeFromRight (8);
    }
    const auto side = juce::jmin (area.getWidth(), area.getHeight());
    return juce::Rectangle<int> (0, 0, side, side).withCentre (area.getCentre())
        .reduced (main_frame::insetFor (context));
}

inline void verifySpace()
{
    for (const bool compact : { false, true })
    {
        const auto context = compact ? presentation::forEditor (300, 200)
                                     : presentation::forEditor (600, 400);
        const juce::Rectangle<int> area = compact ? juce::Rectangle<int> (20, 20, 300, 160)
                                                 : juce::Rectangle<int> (20, 20, 500, 250);
        const auto window = spaceWindow (area, compact, context);
        KirinMeterSession meter {};
        meter.channels = 2;
        meter.field_size = KIRIN_STEREO_FIELD_SIZE;
        meter.field_observation_count = 30;
        meter.balance_state = KIRIN_BALANCE_NUMERIC;
        const auto painted = render (context, [&] (juce::Graphics& g) {
            space_field::paint (g, area, meter, {}, true, compact, context);
        });
        const auto glass = render (context, [&] (juce::Graphics& g) {
            surface_material::paintPanel (g, area.toFloat(), compact ? 0.96f : 0.76f);
            surface_material::paintPanel (g, window.toFloat(), compact ? 0.96f : 0.76f);
        });
        requireInnerShadow (painted, glass, window,
                            "SPACE keeps its frame's inner shadow after the density glass fill");
    }
}

inline void verifyTime()
{
    const auto context = presentation::forEditor (600, 400);
    const juce::Rectangle<int> window (40, 40, 500, 250);
    for (const bool compact : { false, true })
    {
        const auto paint = [&] (juce::Graphics& g, bool framed) {
            time_history::paint (g, window, {}, "30 S", false, compact,
                meter_context::ScaleMode::wide, context, {}, true, framed);
        };
        const auto glass = render (context, [&] (juce::Graphics& g) { paint (g, false); });
        const auto framed = render (context, [&] (juce::Graphics& g) { paint (g, true); });
        requireInnerShadow (framed, glass, window,
                            "TIME HISTORY keeps its inner shadow before status and retained facts");
    }
    const auto paintRun = [&] (juce::Graphics& g, bool framed) {
        run_summary::paint (g, window, {}, 48'000.0, context, nullptr, framed);
    };
    const auto glass = render (context, [&] (juce::Graphics& g) { paintRun (g, false); });
    const auto framed = render (context, [&] (juce::Graphics& g) { paintRun (g, true); });
    requireInnerShadow (framed, glass, window,
                        "TIME RUN keeps its inner shadow before labels and retained run facts");
}

inline void verifyLevel()
{
    // PRE absolute, POST delta, no observations, and rejected/non-finite observations all use
    // this painter in the editor and Capture. A merely non-zero shadow is insufficient: the old
    // layer order retained ~20% of the upper wall's shadow and passed that weaker check.
    const std::array contexts {
        presentation::forEditor (600, 400), presentation::forEditor (900, 600),
        presentation::forOutput (1200, 630, presentation::OutputTarget::capture)
    };
    for (const auto context : contexts)
        for (const int dpi : { 1, 2 })
        {
            const juce::Rectangle<int> window (40, 40, context.logicalWidth - 80,
                                              context.logicalHeight - 80);
            const auto paint = [&] (auto&& painter) {
                juce::Component root;
                root.setSize (context.logicalWidth, context.logicalHeight);
                const key_light::Scope light (root);
                juce::Image image (juce::Image::ARGB, context.logicalWidth * dpi,
                                   context.logicalHeight * dpi, true, juce::NativeImageType {});
                juce::Graphics g (image);
                g.addTransform (juce::AffineTransform::scale (static_cast<float> (dpi)));
                g.fillAll (BG);
                painter (g);
                return image;
            };
            const auto glass = paint ([&] (juce::Graphics& g) {
                surface_material::paintPanel (g, window.toFloat(), 0.62f);
            });
            // An independent uncached frame defines the full shadow after the glass fill.
            const auto reference = paint ([&] (juce::Graphics& g) {
                surface_material::paintPanel (g, window.toFloat(), 0.62f);
                main_frame::uncached::paint (g, window.toFloat());
            });
            for (int state = 0; state < 4; ++state)
            {
                std::vector<KirinMeterHistoryEntry> history (state == 2 ? 0u : 1u);
                if (! history.empty())
                {
                    history.front().last_observed_frames = 48'000u;
                    const auto value = state == 3 ? std::numeric_limits<double>::quiet_NaN()
                                                 : state == 1 ? 0.5 : -14.0;
                    history.front().lufs_m = { value, value, value };
                    history.front().true_peak = { -6.0, -6.0, -6.0 };
                }
                const auto actual = paint ([&] (juce::Graphics& g) {
                    capture_history::paint (g, window, history, state == 1, 48'000.0,
                                            context);
                });
                int actualShadow = 0, referenceShadow = 0, count = 0;
                for (int y = (window.getY() + 2) * dpi; y < (window.getY() + 5) * dpi; ++y)
                    for (int x = (window.getX() + window.getWidth() / 4) * dpi;
                         x < (window.getX() + window.getWidth() * 3 / 4) * dpi; ++x)
                    {
                        const auto brightness = [] (juce::Colour colour) {
                            return (int) colour.getRed() + (int) colour.getGreen()
                                 + (int) colour.getBlue();
                        };
                        const auto substrate = brightness (glass.getPixelAt (x, y));
                        actualShadow += substrate - brightness (actual.getPixelAt (x, y));
                        referenceShadow += substrate - brightness (reference.getPixelAt (x, y));
                        ++count;
                    }
                require (count > 0 && referenceShadow > count * 4,
                         "LEVEL shadow reference is visible and above quantization noise");
                require (actualShadow * 10 >= referenceShadow * 9,
                         "LEVEL retains at least 90% of its frame's upper-wall shadow");
            }
        }
    std::cout << "LEVEL layer order: PASS (24 absolute/delta/empty/non-finite editor/Capture cases)\n";
}
}

inline void verifySurfaceLayerOrderContract()
{
    surface_layer_order::verifySpace();
    surface_layer_order::verifyTime();
    surface_layer_order::verifyLevel();
}
}
