#pragma once

#include "../src/HyphaMaterialCache.h"
#include "../src/HyphaSurfaceMaterial.h"
#include "../src/HyphaTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

// Static surface material is served from device-resolution images while an editor is open. The
// images must look like the material painted directly, including the shadow a recessed panel
// casts above itself and the contact shadow under a raised plate; they must exist only while an
// editor holds the cache, stay within their memory budget, and cost less than painting.
namespace hypha::tests
{
namespace material_cache_contract
{
inline void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Material cache contract failed at line " << line << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_MATERIAL_CACHE_REQUIRE(expression) \
    hypha::tests::material_cache_contract::require ((expression), #expression, __LINE__)

enum class Kind { panel, plate, well };

struct Case
{
    Kind kind;
    juce::Rectangle<float> area;
};

inline void paintCase (juce::Graphics& g, const Case& c)
{
    if (c.kind == Kind::panel)
        surface_material::paintPanel (g, c.area, 0.76f);
    else if (c.kind == Kind::plate)
        surface_material::paintControl (g, c.area, false, false, false);
    else
        surface_material::paintObservationWell (g, c.area);
}

inline juce::Image render (const Case& c, float dpi)
{
    juce::Image image (juce::Image::ARGB, (int) std::ceil (240.0f * dpi), (int) std::ceil (140.0f * dpi), true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (dpi));
    g.fillAll (BG);
    paintCase (g, c);
    return image;
}

inline int largestDifference (const juce::Image& a, const juce::Image& b)
{
    int largest = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
        {
            const auto p = a.getPixelAt (x, y);
            const auto q = b.getPixelAt (x, y);
            largest = std::max ({ largest, std::abs ((int) p.getRed() - (int) q.getRed()),
                                  std::abs ((int) p.getGreen() - (int) q.getGreen()),
                                  std::abs ((int) p.getBlue() - (int) q.getBlue()) });
        }
    return largest;
}

inline std::optional<juce::SharedResourcePointer<material_cache::Store>> sharedStore()
{
    return juce::SharedResourcePointer<material_cache::Store>::getSharedObjectWithoutCreating();
}

// The first request paints directly, the second keeps an image, and from then on the image is
// drawn. Every one of them looks like the direct paint.
inline void verifyCachedMaterialLooksPainted()
{
    for (const auto dpi : { 1.0f, 2.0f })
        for (const auto& c : { Case { Kind::panel, { 20.0f, 20.0f, 180.0f, 90.0f } },
                               Case { Kind::plate, { 20.0f, 30.0f, 64.0f, 22.0f } },
                               Case { Kind::well, { 20.0f, 20.0f, 200.0f, 100.0f } } })
        {
            const auto painted = render (c, dpi);
            material_cache::Lifetime editor;
            const auto& store = *editor.store;
            KIRIN_MATERIAL_CACHE_REQUIRE (largestDifference (painted, render (c, dpi)) == 0);
            KIRIN_MATERIAL_CACHE_REQUIRE (store.bytes() == 0u);
            const auto kept = render (c, dpi);
            KIRIN_MATERIAL_CACHE_REQUIRE (store.bytes() > 0u);
            const auto cached = render (c, dpi);
            // Composing translucent layers in an image first rounds once more; a missing shadow
            // differs by six or more levels.
            KIRIN_MATERIAL_CACHE_REQUIRE (largestDifference (painted, kept) <= 3);
            KIRIN_MATERIAL_CACHE_REQUIRE (largestDifference (painted, cached) <= 3);
        }
}

// Without an editor nothing is kept; the images go with the last editor; however many sizes pass
// through, the images kept stay within the budget.
inline void verifyCacheLifetimeAndBudget()
{
    KIRIN_MATERIAL_CACHE_REQUIRE (! sharedStore().has_value());
    juce::Image target (juce::Image::ARGB, 16, 16, true);
    {
        material_cache::Lifetime editor;
        juce::Graphics g (target);
        g.addTransform (juce::AffineTransform::scale (2.0f));
        for (int size = 0; size < 24; ++size)
            for (int request = 0; request < 3; ++request)
                surface_material::paintObservationWell (g, { 0.0f, 0.0f, 600.0f + (float) size, 400.0f });
        KIRIN_MATERIAL_CACHE_REQUIRE (editor.store->bytes() > 0u);
        KIRIN_MATERIAL_CACHE_REQUIRE (editor.store->bytes() <= material_cache::Store::budgetBytes);
    }
    KIRIN_MATERIAL_CACHE_REQUIRE (! sharedStore().has_value());
}

// The 300% FREQ observation window and a large panel at DPI 2, the heaviest static material.
inline void verifyCachedMaterialIsCheaper()
{
    material_cache::Lifetime editor;
    const auto median = [] (auto&& paint) {
        std::vector<double> samples;
        for (int index = 0; index < 23; ++index)
        {
            const auto start = juce::Time::getMillisecondCounterHiRes();
            paint();
            if (index >= 3)
                samples.push_back (juce::Time::getMillisecondCounterHiRes() - start);
        }
        std::sort (samples.begin(), samples.end());
        return samples[samples.size() / 2];
    };
    for (const auto& c : { Case { Kind::well, { 4.0f, 4.0f, 733.0f, 300.0f } },
                           Case { Kind::panel, { 4.0f, 4.0f, 560.0f, 250.0f } } })
    {
        juce::Image image (juce::Image::ARGB, 1'490, 620, true);
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (2.0f));
        const auto painted = median ([&] {
            const juce::Graphics::ScopedSaveState saved (g);
            if (c.kind == Kind::well)
                surface_material::uncached::paintObservationWell (g, c.area);
            else
                surface_material::uncached::paintPanel (g, c.area, 0.76f, 4.0f, false);
        });
        const auto cached = median ([&] {
            const juce::Graphics::ScopedSaveState saved (g);
            paintCase (g, c);
        });
        std::cout << "Material " << (c.kind == Kind::well ? "well" : "panel") << " at DPI 2: painted "
                  << painted << " ms, cached " << cached << " ms\n";
        KIRIN_MATERIAL_CACHE_REQUIRE (cached < painted);
    }
}
}

inline void verifyMaterialCacheContract()
{
    material_cache_contract::verifyCachedMaterialLooksPainted();
    material_cache_contract::verifyCacheLifetimeAndBudget();
    material_cache_contract::verifyCachedMaterialIsCheaper();
}
}
