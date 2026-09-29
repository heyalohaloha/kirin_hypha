#pragma once

#include "../src/HyphaSurfaceMaterial.h"
#include "../src/HyphaTheme.h"

#include <cmath>
#include <iostream>

// The editor's material light (2026-09-29): cards are quiet, with a fine outline and no reflection
// shape; the page's main window catches the light on its edge, brightest along the top; the
// instrument frame stays at the edge of the editor.
namespace hypha::tests
{
inline bool verifyMaterialLight()
{
    const auto brightness = [] (const juce::Image& image, int x, int y) {
        return image.getPixelAt (x, y).getPerceivedBrightness(); };
    const auto check = [] (bool condition, const char* what) {
        if (! condition)
            std::cerr << "material light: " << what << '\n';
        return condition;
    };

    juce::Image panel (juce::Image::RGB, 120, 60, true);
    {
        juce::Graphics g (panel);
        g.fillAll (BG);
        surface_material::paintPanel (g, panel.getBounds().toFloat(), 0.96f, 5.0f);
    }
    // A card is quiet: a fine outline (on the unshadowed right wall, brighter than the glass
    // beside it) and glass without a reflection shape, so a row reads the same left and right.
    if (! check (panel.getPixelAt (60, 30) != BG, "a card has its own material")
        || ! check (brightness (panel, 118, 30) > brightness (panel, 110, 30), "a card's outline")
        || ! check (std::abs (brightness (panel, 20, 30) - brightness (panel, 100, 30)) < 0.004f,
                    "no reflection shape on a card"))
        return false;

    // The page's main window catches the light on its edge, brightest along the top, and its glass
    // holds no reflection shape either.
    juce::Image well (juce::Image::RGB, 200, 120, true);
    {
        juce::Graphics g (well);
        g.fillAll (BG);
        surface_material::paintObservationWell (g, well.getBounds().toFloat());
    }
    if (! check (brightness (well, 100, 2) > brightness (well, 100, 60) + 0.05f, "the lit top edge")
        || ! check (brightness (well, 100, 2) > brightness (well, 2, 60), "the top brighter than the sides")
        || ! check (std::abs (brightness (well, 50, 60) - brightness (well, 150, 60)) < 0.004f,
                    "no reflection shape on the main window"))
        return false;

    juce::Image frame (juce::Image::RGB, 300, 200, true);
    {
        juce::Graphics g (frame);
        g.fillAll (BG);
        surface_material::paintInstrumentFrame (g, frame.getBounds().toFloat(), false);
    }
    return check (frame.getPixelAt (150, 1) != BG, "the instrument frame at the edge")
        && check (frame.getPixelAt (150, 100) == BG, "the instrument frame leaves the body");
}
}
