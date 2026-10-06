#pragma once

#include "../src/HyphaMainFrame.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

// A frame's oracle is its canonical device-resolution raster, including the first paint and a
// paint with no editor. The native vector renderer and bitmap compositor have different
// antialiasing at fractional device origins; changing between them after the first paint caused
// a visible jump. These cases use shipping FREQ layout and actual inherited graphics transforms,
// then move a frame already in the cache without changing its size or material.
namespace hypha::tests::main_frame_cache_contract
{
inline void require (bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "Main frame cache contract failed: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

inline int difference (const juce::Image& a, const juce::Image& b)
{
    require (a.getBounds() == b.getBounds(), "same raster dimensions");
    const juce::Image::BitmapData left (a, juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData right (b, juce::Image::BitmapData::readOnly);
    int maximum = 0;
    for (int y = 0; y < left.height; ++y)
    {
        if (left.pixelStride == right.pixelStride
            && std::memcmp (left.getLinePointer (y), right.getLinePointer (y),
                            (size_t) left.width * (size_t) left.pixelStride) == 0)
            continue;
        for (int x = 0; x < left.width; ++x)
        {
            const auto p = left.getPixelColour (x, y), q = right.getPixelColour (x, y);
            maximum = std::max ({ maximum, std::abs ((int) p.getRed() - (int) q.getRed()),
                                 std::abs ((int) p.getGreen() - (int) q.getGreen()),
                                 std::abs ((int) p.getBlue() - (int) q.getBlue()),
                                 std::abs ((int) p.getAlpha() - (int) q.getAlpha()) });
        }
    }
    return maximum;
}

inline juce::Image render (int width, int height, juce::Rectangle<int> body,
                           juce::Rectangle<float> window, const juce::Component& component,
                           float dpi, bool software, juce::Point<float> displacement,
                           const juce::AffineTransform& inherited = {})
{
    const auto rasterWidth = (int) std::ceil ((width + 48.0f) * dpi);
    const auto rasterHeight = (int) std::ceil ((height + 48.0f) * dpi);
    auto output = software
        ? juce::Image (juce::Image::ARGB, rasterWidth, rasterHeight, true, juce::SoftwareImageType {})
        : juce::Image (juce::Image::ARGB, rasterWidth, rasterHeight, true, juce::NativeImageType {});
    juce::Graphics g (output);
    g.addTransform (juce::AffineTransform::scale (dpi));
    g.fillAll (BG);
    g.addTransform (juce::AffineTransform::translation ((float) body.getX() + displacement.x,
                                                      (float) body.getY() + displacement.y));
    g.addTransform (inherited);
    const key_light::Scope light (component);
    main_frame::paint (g, window);
    return output;
}

inline void verifyColdWarmAndMovedOrigins()
{
    int cases = 0;
    int maximum = 0;
    for (const bool software : { false, true })
        for (const auto width : { 300, 375, 450, 501, 600, 900 })
    {
        const auto height = width * 2 / 3;
        juce::Component root, component;
        root.setSize (width, height);
        root.getProperties().set (key_light::rootProperty, true);
        observatory::View shell (observatory::Role::post);
        shell.setSize (width, height);
        shell.setDomain (observatory::Domain::frequency);
        const auto body = shell.analysisBodyBounds();
        root.addChildComponent (component);
        component.setBounds (body);
        const auto window = spectrum_geometry::dataPlotBoundsFor (component.getLocalBounds().toFloat());
        for (const auto dpi : { 1.0f, 1.25f, 1.5f, 2.0f })
            for (const auto inherited : { juce::AffineTransform {},
                    juce::AffineTransform::scale (1.07f, 0.94f).rotated (0.025f).translated (0.37f, 0.19f) })
        {
            const juce::Point<float> origin {}, moved { 7.57f, 5.83f };
            const auto paintAt = [&] (juce::Point<float> offset) {
                return render (width, height, body, window, component, dpi, software, offset, inherited); };
            const auto temporary = paintAt (origin);
            const auto temporaryMoved = paintAt (moved);
            {
                material_cache::Lifetime editor;
                const auto first = paintAt (origin);
                require (editor.store->bytes() > 0u, "first frame raster is kept");
                const auto warm = paintAt (origin);
                const auto movedWarm = paintAt (moved);
                const auto back = paintAt (origin);
                maximum = std::max ({ maximum, difference (temporary, first), difference (first, warm),
                                     difference (temporaryMoved, movedWarm), difference (first, back) });
                require (maximum <= 3, "no-store, first, warm and moved frame agree within 3 levels");
                require (editor.store->bytes() <= editor.store->budget(), "canonical frames obey cache budget");
            }
            ++cases;
        }
    }
    std::cout << "Main frame canonical raster: " << cases << " native/software layout/transform cases, max "
              << maximum << " levels\n";
}

inline void verifyEmptyAndOversizeFallback()
{
    juce::Component component;
    component.setSize (300, 200);
    const juce::Rectangle<int> body { 0, 0, 300, 200 };
    const auto empty = render (300, 200, body, {}, component, 2.0f, false, {});
    const auto oversized = render (300, 200, body, { 15.25f, 20.75f, 5'000.0f, 80.0f },
                                  component, 2.0f, false, {});
    material_cache::Lifetime editor;
    require (difference (empty, render (300, 200, body, {}, component, 2.0f, false, {})) == 0,
             "empty frame is silent and unchanged");
    require (difference (oversized, render (300, 200, body, { 15.25f, 20.75f, 5'000.0f, 80.0f },
                                          component, 2.0f, false, {})) == 0,
             "oversize frame consistently uses bounded direct fallback");
    require (editor.store->bytes() == 0u, "empty and oversized frames allocate no cached raster");
}

// Cold resize is measured separately from the steady repaint: each of these material dimensions
// is new. Keep the sampled cost visible in the test log without asserting a machine-specific
// absolute time. Existing page and cache contracts retain the steady repaint limits.
inline void measureColdResize (bool software)
{
    juce::Component component;
    component.setSize (900, 600);
    auto output = software
        ? juce::Image (juce::Image::ARGB, 1'800, 1'200, true, juce::SoftwareImageType {})
        : juce::Image (juce::Image::ARGB, 1'800, 1'200, true, juce::NativeImageType {});
    juce::Graphics g (output);
    g.addTransform (juce::AffineTransform::scale (2.0f));
    g.fillAll (BG);
    const key_light::Scope light (component);
    std::vector<double> samples;
    material_cache::Lifetime editor;
    for (int index = 0; index < 16; ++index)
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        main_frame::paint (g, { 40.13f, 55.29f, 650.0f + (float) index, 350.0f });
        samples.push_back (juce::Time::getMillisecondCounterHiRes() - start);
    }
    std::sort (samples.begin(), samples.end());
    require (editor.store->bytes() <= editor.store->budget(), "cold resize obeys bounded memory");
    std::cout << "Main frame cold resize (" << (software ? "software" : "native")
              << "): median " << samples[samples.size() / 2] << " ms, max " << samples.back() << " ms\n";
}
}

namespace hypha::tests
{
inline void verifyMainFrameCacheContract()
{
    main_frame_cache_contract::verifyColdWarmAndMovedOrigins();
    main_frame_cache_contract::verifyEmptyAndOversizeFallback();
    main_frame_cache_contract::measureColdResize (false);
    main_frame_cache_contract::measureColdResize (true);
}
}
