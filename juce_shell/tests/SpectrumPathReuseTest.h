#pragma once
#include "SpectrumPerformanceFixture.h"
#include "../src/HyphaSpectrumPainter.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaSpectrumPostBody.h"
#include "../src/HyphaMonotoneCurve.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void verifySpectrumPathReuse()
{
    const auto check = [] (bool ok, const char* what)
    { if (! ok) { std::cerr << "FREQ path reuse failed: " << what << '\n'; std::exit (1); } };
    const auto checkCapacity = [&check] (const spectrum_painter::PathWorkspace& workspace)
    {
        for (size_t slot = 0; slot < spectrum_painter::PathWorkspace::count; ++slot)
        {
            int coordinates = 0;
            juce::Path::Iterator iterator (workspace.existing (slot));
            while (iterator.next())
                switch (iterator.elementType)
                {
                    case juce::Path::Iterator::startNewSubPath:
                    case juce::Path::Iterator::lineTo: coordinates += 3; break;
                    case juce::Path::Iterator::quadraticTo: coordinates += 5; break;
                    case juce::Path::Iterator::cubicTo: coordinates += 7; break;
                    case juce::Path::Iterator::closePath: coordinates += 1; break;
                }
            check (coordinates <= workspace.reserved (slot), "path fits reserved storage");
        }
    };
    const auto frame = spectrumPerformanceFixture();
    spectrum_painter::SpectrumBins pre {}, post {}, delta {}, mark {};
    spectrum_painter::SpectrumValidity valid {}, markValid {};
    for (size_t i = 0; i < pre.size(); ++i)
    {
        pre[i] = frame.pre_dbfs[i]; post[i] = frame.post_dbfs[i];
        delta[i] = frame.display_db[i]; mark[i] = delta[i] * 0.7f;
        valid[i] = (i % 31u != 0u); markValid[i] = (i % 19u != 0u);
    }
    spectrum_painter::PathWorkspace workspace;
    uint64_t warmAllocations = 0;
    for (const auto size : { 1, 2, 3 })
        for (const bool rich : { false, true })
            for (const bool marked : { false, true })
            {
                const int width = 288 * size, height = 150 * size;
                const juce::Rectangle<float> plot { 27.0f * size, 14.0f * size,
                                                    230.0f * size, 112.0f * size };
                const auto render = [&] (spectrum_painter::PathWorkspace* paths)
                {
                    juce::Image image (juce::Image::ARGB, width, height, true,
                                       juce::SoftwareImageType());
                    juce::Graphics g (image);
                    g.fillAll (juce::Colour (0xff19242b));
                    spectrum_painter::paintCurves (g, plot, (float) size, pre, post, delta, valid,
                        marked ? &mark : nullptr, marked ? &markValid : nullptr, 96.0f, 1.0f, rich, paths);
                    return image;
                };
                const auto reference = render (nullptr);
                const auto cached = render (&workspace);
                spectrum_painter::SpectrumBins xs {}, ys {};
                for (size_t i = 0; i < xs.size(); ++i)
                {
                    xs[i] = juce::jmap (spectrum_geometry::bandCentreNormalisedX (i), plot.getX(), plot.getRight());
                    ys[i] = spectrum_painter::yForMagnitude (pre[i], plot, 96.0f);
                }
                check (workspace.existing (0) == monotone_curve::make (xs, ys, static_cast<const spectrum_painter::SpectrumValidity*> (nullptr), true),
                       "curve geometry matches the original builder");
                const juce::Image::BitmapData a (reference, juce::Image::BitmapData::readOnly);
                const juce::Image::BitmapData b (cached, juce::Image::BitmapData::readOnly);
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                        check (a.getPixelColour (x, y) == b.getPixelColour (x, y), "pixel parity");
                checkCapacity (workspace);
                const auto allocations = workspace.allocations();
                render (&workspace);
                warmAllocations += workspace.allocations() - allocations;
            }
    std::cout << "FREQ curve pixels match; warm path allocations: " << warmAllocations << '\n';
    check (warmAllocations == 0, "warm curve path allocations");
    // Alternate large peaks and holes so runs repeatedly enter and leave colour buckets.
    for (size_t i = 0; i < delta.size(); ++i)
    {
        const float magnitude = static_cast<float> (i % 25u) * 0.95f;
        delta[i] = (i % 2u == 0u) ? magnitude : -magnitude;
        valid[i] = (i % 7u != 0u);
    }
    const juce::Rectangle<float> stressPlot { 27, 14, 230, 112 };
    const auto renderStress = [&] (spectrum_painter::PathWorkspace* paths)
    {
        juce::Image image (juce::Image::ARGB, 288, 150, true, juce::SoftwareImageType());
        juce::Graphics g (image); g.fillAll (juce::Colour (0xff19242b));
        spectrum_painter::paintCurves (g, stressPlot, 1, pre, post, delta, valid, &mark, &markValid,
                                        96, 1, true, paths);
        return image;
    };
    const auto stressReference = renderStress (nullptr), stressCached = renderStress (&workspace);
    checkCapacity (workspace);
    const auto stressAllocations = workspace.allocations();
    renderStress (&workspace);
    check (workspace.allocations() == stressAllocations, "warm alternating peaks and holes");
    const juce::Image::BitmapData stressA (stressReference, juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData stressB (stressCached, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < stressA.height; ++y)
        for (int x = 0; x < stressA.width; ++x)
            check (stressA.getPixelColour (x, y) == stressB.getPixelColour (x, y), "alternating peaks pixel parity");
    absolute_spectrum::History history;
    history.append (frame);
    for (const int mode : { 0, 1, 2, 3 })
        for (const int size : { 1, 2, 3 })
        {
            const int width = 288 * size, height = 150 * size;
            const juce::Rectangle<float> plot { 27.0f * size, 14.0f * size, 230.0f * size, 112.0f * size };
            const auto render = [&] (spectrum_painter::PathWorkspace* paths)
            {
                juce::Image image (juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
                juce::Graphics g (image); g.fillAll (juce::Colour (0xff19242b));
                if (mode == 0) spectrum_painter::paintPostCurve (g, plot, (float) size, post, 96, 1, paths);
                else if (mode == 1) spectrum_painter::paintAbsolute (g, plot, (float) size, post, pre,
                    history, presentation::defaultContext(), false, {}, nullptr, 10, 22000, paths);
                else if (mode == 2) spectrum_painter::paintMidSide (g, plot, (float) size, pre, post, 96, 1, paths);
                else spectrum_painter::post_body::stoppedPeak (g, plot, post, 96, (float) size, 1, paths);
                return image;
            };
            const auto reference = render (nullptr), cached = render (&workspace);
            spectrum_painter::SpectrumBins xs {}, ys {};
            for (size_t i = 0; i < xs.size(); ++i)
            {
                xs[i] = juce::jmap (spectrum_geometry::bandCentreNormalisedX (i), plot.getX(), plot.getRight());
                ys[i] = spectrum_painter::yForMagnitude (mode == 2 ? pre[i] : post[i], plot, 96.0f);
            }
            check (workspace.existing (0) == monotone_curve::make (xs, ys,
                   static_cast<const spectrum_painter::SpectrumValidity*> (nullptr), mode != 2),
                   "mode geometry matches the original builder");
            const auto allocations = workspace.allocations();
            render (&workspace);
            check (workspace.allocations() == allocations, "warm path allocations after mode change");
            const juce::Image::BitmapData a (reference, juce::Image::BitmapData::readOnly);
            const juce::Image::BitmapData b (cached, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    check (a.getPixelColour (x, y) == b.getPixelColour (x, y), "mode pixel parity");
        }
    for (const bool absolute : { false, true })
    {
        SpectrumComponent view;
        view.setSize (576, 300); view.setAbsoluteObservation (absolute);
        view.setSignalActive (true); view.setSnapshot (frame);
        juce::Image image (juce::Image::ARGB, 576, 300, true, juce::SoftwareImageType());
        const auto paint = [&] { image.clear (image.getBounds()); juce::Graphics g (image); view.paint (g); };
        paint();
        const auto allocations = view.curvePathAllocationsForTest();
        std::cout << "FREQ component " << (absolute ? "POST" : "DELTA")
                  << " path allocations after warmup: " << allocations << '\n';
        check (allocations > 0, "component owns the curve paths");
        const auto previous = image.createCopy();
        paint();
        check (view.curvePathAllocationsForTest() == allocations, "component warm path allocations");
        const juce::Image::BitmapData a (previous, juce::Image::BitmapData::readOnly);
        const juce::Image::BitmapData b (image, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                check (a.getPixelColour (x, y) == b.getPixelColour (x, y), "component pixel parity");
    }
}
}
