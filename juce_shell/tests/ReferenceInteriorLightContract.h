#pragma once

#include "../src/HyphaReferenceComparisonView.h"
#include "../src/HyphaReferenceComponent.h"
#include "../src/HyphaReferenceTonalView.h"
#include "../src/HyphaReferenceVisualLayout.h"
#include "../src/HyphaReferenceVisuals.h"
#include "../src/HyphaReferenceWindowMaterial.h"
#include "../src/HyphaSurfaceMaterial.h"

#include <cstdlib>
#include <functional>
#include <iostream>

namespace hypha::tests::reference_interior_light
{
inline void require (bool ok, const char* message)
{
    if (ok) return;
    std::cerr << "Reference interior light contract failed: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

template <typename Paint>
juce::Image render (int width, int height, Paint&& paint)
{
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics g (image);
    g.fillAll (BG);
    paint (g);
    return image;
}

inline void requireShadow (const juce::Image& actual, const juce::Image& glass,
                          const juce::Image& expected, juce::Rectangle<int> strip)
{
    int darkness = 0, count = 0;
    for (int y = strip.getY(); y < strip.getBottom(); ++y)
        for (int x = strip.getX(); x < strip.getRight(); ++x)
        {
            const auto got = actual.getPixelAt (x, y), want = expected.getPixelAt (x, y);
            const auto plain = glass.getPixelAt (x, y);
            require (std::abs (got.getRed() - want.getRed()) <= 1
                         && std::abs (got.getGreen() - want.getGreen()) <= 1
                         && std::abs (got.getBlue() - want.getBlue()) <= 1,
                     "actual glass keeps the shared window shadow at its editor coordinates");
            darkness += int (plain.getRed()) + int (plain.getGreen()) + int (plain.getBlue())
                      - int (got.getRed()) - int (got.getGreen()) - int (got.getBlue());
            ++count;
        }
    require (count > 0 && darkness > count * 2,
             "the window's upper wall stays visibly recessed after its glass fill");
}

inline juce::Rectangle<int> topStrip (juce::Rectangle<float> window)
{
    const auto area = window.toNearestInt();
    return { area.getX() + area.getWidth() / 4, area.getY() + 2,
             area.getWidth() / 2, 3 };
}

template <typename View, typename Fill>
void verifyChild (View& view, Fill&& fill)
{
    const auto bounds = view.getLocalBounds().toFloat();
    const auto actual = render (view.getWidth(), view.getHeight(), [&] (juce::Graphics& g) {
        view.paint (g);
    });
    const key_light::Scope light (view);
    const auto glass = render (view.getWidth(), view.getHeight(), fill);
    const auto expected = render (view.getWidth(), view.getHeight(), [&] (juce::Graphics& g) {
        fill (g);
        reference_ui::window_material::paintInterior (g, bounds, bounds);
    });
    requireShadow (actual, glass, expected, topStrip (bounds));
}

inline void verifyChildren()
{
    juce::Component root;
    root.getProperties().set (key_light::rootProperty, true);
    root.setSize (900, 600);
    reference_ui::ComparisonView comparison;
    reference_ui::TonalView tonal;
    root.addAndMakeVisible (comparison);
    root.addAndMakeVisible (tonal);
    for (const int editorWidth : { 300, 900 })
    {
        const auto context = presentation::forEditor (editorWidth, editorWidth * 2 / 3);
        root.setSize (context.logicalWidth, context.logicalHeight);
        comparison.setBounds (20, 60, editorWidth - 40, context.logicalHeight - 80);
        comparison.update ({}, -1, context, false);
        verifyChild (comparison, [&] (juce::Graphics& g) {
            surface_material::paintObservationWell (g, comparison.getLocalBounds().toFloat(), false);
        });
        tonal.setBounds (20, 60, editorWidth - 40, context.logicalHeight - 80);
        tonal.update ({}, context, false, {}, {});
        verifyChild (tonal, [&] (juce::Graphics& g) {
            surface_material::paintPanel (g, tonal.getLocalBounds().toFloat(), 0.72f);
        });
    }
}

inline void verifyConfigured()
{
    juce::Component root;
    root.getProperties().set (key_light::rootProperty, true);
    root.setSize (900, 600);
    const key_light::Scope light (root);
    const auto context = presentation::forEditor (900, 600);
    reference_ui::State state;
    state.viewBindings = { "dynamics", "loudness" };
    state.presentationLayout = "equal";
    for (const auto width : { 500.0f, 700.0f })
    {
        const juce::Rectangle<float> window (40, 40, width, 300);
        const auto cells = reference_ui::referenceVisualCells (window, 2, "equal");
        const auto fill = [&] (juce::Graphics& g) {
            for (int index = 0; index < 2; ++index)
                surface_material::paintPanel (g, cells[size_t (index)], 0.72f);
        };
        const auto actual = render (900, 600, [&] (juce::Graphics& g) {
            require (reference_ui::paintConfiguredReferenceViews (g, window, state, context),
                     "the actual configured-chart route paints its observation");
        });
        const auto glass = render (900, 600, fill);
        const auto expected = render (900, 600, [&] (juce::Graphics& g) {
            fill (g);
            reference_ui::window_material::paintInterior (g, window, window);
        });
        requireShadow (actual, glass, expected, topStrip (cells[0]));
        if (std::equal_to<float> {} (cells[1].getY(), window.getY()))
            requireShadow (actual, glass, expected, topStrip (cells[1]));
        else
        {
            // A lower stacked card remains quiet: it shares the full observation window, so
            // its own upper border cannot acquire a second main-window shadow or gold frame.
            const auto strip = topStrip (cells[1]);
            for (int y = strip.getY(); y < strip.getBottom(); ++y)
                for (int x = strip.getX(); x < strip.getRight(); ++x)
                    require (actual.getPixelAt (x, y) == glass.getPixelAt (x, y),
                             "a lower configured card never gains its own main-window frame");
        }
        for (int x = 60; x < int (window.getRight()) - 20; ++x)
            require (actual.getPixelAt (x, 38) == BG,
                     "configured cells leave the one outer bronze frame to their parent");
    }
}

inline void verify()
{
    verifyChildren();
    verifyConfigured();
}
}
