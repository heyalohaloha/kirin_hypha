#pragma once

#include "../src/HyphaMaterialCache.h"
#include "../src/HyphaObservatoryResizeContract.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaSpectrumComponent.h"
#include "CompactReviewShowcase.h"
#include "SpectrumTerrainShowcase.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <vector>

// Above 300% the editor draws the Inspection View magnified, only on steps that keep it on whole
// device pixels (HyphaObservatoryResizeContract.h). On a DPI 2 display 450% and 600% draw the
// 900 x 600 layout at 3 and 4 device pixels per point: 2.25 and 4 times the pixels of 300%. The
// heaviest pages must grow with the pixels only; a cached image resampled on every paint, or a
// material cache too small to hold the magnified images, would multiply the cost instead.
namespace hypha::tests
{
namespace magnified_inspection
{
inline void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Magnified Inspection contract failed at line " << line << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_MAGNIFIED_REQUIRE(expression) \
    hypha::tests::magnified_inspection::require ((expression), #expression, __LINE__)

// Median of the steady paints of `paint` into the 900 x 600 layout drawn at `scale` device pixels
// per point, after the first paints have built what an open editor keeps.
inline double medianPaint (float scale, const std::function<void (juce::Graphics&)>& paint)
{
    juce::Image surface (juce::Image::ARGB, (int) std::ceil (900.0f * scale), (int) std::ceil (600.0f * scale),
                         true, juce::NativeImageType {});
    std::vector<double> samples;
    for (int index = 0; index < 13; ++index)
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        {
            juce::Graphics g (surface);
            g.addTransform (juce::AffineTransform::scale (scale));
            paint (g);
        }
        if (index >= 3)
            samples.push_back (juce::Time::getMillisecondCounterHiRes() - start);
    }
    std::sort (samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

struct Page
{
    const char* name;
    std::function<void (juce::Graphics&)> paint;
};
}

inline void verifyMagnifiedInspectionBudget()
{
    using namespace magnified_inspection;
    // The steps a DPI 2 display offers are the device scales measured below.
    KIRIN_MAGNIFIED_REQUIRE (observatory::magnifiedStep (2.0f, 0).width == 1350);
    KIRIN_MAGNIFIED_REQUIRE (observatory::magnifiedStep (2.0f, 1).width == 1800);
    KIRIN_MAGNIFIED_REQUIRE (observatory::displayViewport (1350, 900).scale == 1.5f);
    KIRIN_MAGNIFIED_REQUIRE (observatory::displayViewport (1800, 1200).scale == 2.0f);

    material_cache::Lifetime editorMaterial;
    observatory::View shell (observatory::Role::post);
    shell.setSize (900, 600);
    shell.setObservatoryFrame (compact_review::frame(), true);
    shell.setWatchDisplay (compact_review::watch(), true);
    shell.setHistory (compact_review::history());
    SpectrumComponent landscape;
    landscape.setAbsoluteObservation (true);
    landscape.setSignalActive (true);
    landscape.setPresentationContext (presentation::forEditor (900, 600));
    for (int index = 0; index < freq_showcase::frameCount; ++index)
        landscape.setSnapshot (freq_showcase::frame (index));
    const auto viewPage = [&shell] (observatory::Domain domain) {
        return [&shell, domain] (juce::Graphics& g) {
            if (shell.domain() != domain)
                shell.setDomain (domain);
            shell.paintEntireComponent (g, true);
        };
    };
    const std::vector<Page> pages {
        { "LEVEL", viewPage (observatory::Domain::level) },
        { "TIME", viewPage (observatory::Domain::time) },
        { "SPACE", viewPage (observatory::Domain::space) },
        { "FREQ landscape", [&shell, &landscape] (juce::Graphics& g) {
              if (shell.domain() != observatory::Domain::frequency)
                  shell.setDomain (observatory::Domain::frequency);
              shell.setExternalAnalysisBodyActive (true);
              shell.paintEntireComponent (g, true);
              const auto body = shell.analysisBodyBounds();
              landscape.setBounds (body);
              const juce::Graphics::ScopedSaveState saved (g);
              g.addTransform (juce::AffineTransform::translation ((float) body.getX(), (float) body.getY()));
              landscape.paintEntireComponent (g, true);
              shell.setExternalAnalysisBodyActive (false);
          } },
    };
    for (const auto& page : pages)
    {
        const auto inspection = medianPaint (2.0f, page.paint);
        const auto step450 = medianPaint (3.0f, page.paint);
        const auto step600 = medianPaint (4.0f, page.paint);
        std::cout << "Magnified " << page.name << " on DPI 2: 300% " << inspection << " ms, 450% " << step450
                  << " ms, 600% " << step600 << " ms\n";
        // 2.25 and 4 times the pixels, with room for timing noise; resampling costs far more.
        KIRIN_MAGNIFIED_REQUIRE (step450 < inspection * 2.25 * 1.6 + 2.0);
        KIRIN_MAGNIFIED_REQUIRE (step600 < inspection * 4.0 * 1.6 + 2.0);
    }
    std::cout << "Magnified material cache: " << editorMaterial.store->bytes() / (1024 * 1024) << " MB held of "
              << editorMaterial.store->budget() / (1024 * 1024) << " MB\n";
    KIRIN_MAGNIFIED_REQUIRE (editorMaterial.store->bytes() <= editorMaterial.store->budget());
    KIRIN_MAGNIFIED_REQUIRE (editorMaterial.store->budget() == material_cache::Store::budgetBytes * 4u);
}
}
