#pragma once

#include "../src/HyphaKeyLight.h"
#include "../src/HyphaSurfaceMaterial.h"
#include "../src/HyphaTheme.h"

#include <cmath>
#include <iostream>

// The editor's material light (2026-09-29; stages 2 and 3, 2026-10-01): cards are quiet, with a
// fine outline and no reflection shape; each page's main window stands in a bronze frame that the
// editor's one key light reaches where the frame comes nearest it, and its glass stays quiet;
// control plates catch the same light on their upper bevel at the same place; the instrument frame
// stays at the edge of the editor.
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

    // An editor 600 x 300 with the light above it at x = 252: one window left of the light, one
    // right of it, and a control plate under each.
    juce::Component editor;
    editor.setSize (600, 300);
    juce::Image page (juce::Image::RGB, 600, 300, true);
    const juce::Rectangle<float> left (40.0f, 60.0f, 180.0f, 120.0f), right (380.0f, 60.0f, 180.0f, 120.0f);
    const juce::Rectangle<float> leftPlate (40.0f, 240.0f, 120.0f, 24.0f), rightPlate (440.0f, 240.0f, 120.0f, 24.0f);
    {
        juce::Graphics g (page);
        g.fillAll (BG);
        const key_light::Scope light (editor);
        for (const auto& window : { left, right })
            surface_material::paintObservationWell (g, window);
        for (const auto& plate : { leftPlate, rightPlate })
            surface_material::paintPanel (g, plate, 0.94f, 3.0f, true);
    }
    const auto ring = main_frame::measuresFor (key_light::overEditor (600.0f, 300.0f)).ring;
    const auto bevelY = juce::roundToInt (left.getY() - 0.5f * ring);
    const auto at = [] (const juce::Rectangle<float>& area, float fraction) {
        return juce::roundToInt (area.getX() + fraction * area.getWidth()); };
    // The frame stands around each window, and the light reaches it where it comes nearest: the
    // right end of the window left of the light, the left end of the one right of it.
    if (! check (page.getPixelAt (at (left, 0.5f), bevelY) != BG
                 && page.getPixelAt (juce::roundToInt (left.getX() - 0.5f * ring), 120) != BG
                 && page.getPixelAt (at (left, 0.5f), juce::roundToInt (left.getBottom() + 0.5f * ring)) != BG,
                 "a main window stands in its frame")
        || ! check (brightness (page, at (left, 0.95f), bevelY) > brightness (page, at (left, 0.05f), bevelY) + 0.02f,
                    "the light lands where the frame comes nearest (left of the light)")
        || ! check (brightness (page, at (right, 0.05f), bevelY) > brightness (page, at (right, 0.95f), bevelY) + 0.02f,
                    "the light lands where the frame comes nearest (right of the light)"))
        return false;
    // The glass itself stays quiet: no lit edge inside, no reflection shape across it.
    const auto glassTop = juce::roundToInt (left.getY()) + 2;
    if (! check (brightness (page, at (left, 0.5f), glassTop) <= brightness (page, at (left, 0.5f), 120) + 0.01f,
                 "no light inside the glass")
        || ! check (std::abs (brightness (page, at (left, 0.25f), 120) - brightness (page, at (left, 0.6f), 120)) < 0.004f,
                    "no reflection shape on the main window"))
        return false;
    // A plate's upper bevel catches the same light at the same place.
    const auto plateY = juce::roundToInt (leftPlate.getY()) + 1;
    if (! check (brightness (page, at (leftPlate, 0.9f), plateY) > brightness (page, at (leftPlate, 0.1f), plateY),
                 "a plate left of the light is lit at its right end")
        || ! check (brightness (page, at (rightPlate, 0.1f), plateY) > brightness (page, at (rightPlate, 0.9f), plateY),
                    "a plate right of the light is lit at its left end"))
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
