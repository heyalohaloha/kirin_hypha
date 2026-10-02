#pragma once

#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTheme.h"

#include <cstdlib>
#include <iostream>

// The TIME history range menu opens from a drawn arrow, not a font glyph. JUCE 7 draws a label in
// one typeface with no fallback and Windows' label fonts have no U+25BE, so the glyph was an empty
// box there. With no text there is nothing for a font to miss: every platform draws this path.
namespace hypha::tests
{
namespace menu_arrow_contract
{
inline void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Menu arrow contract failed at line " << line << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_MENU_ARROW_REQUIRE(expression) \
    hypha::tests::menu_arrow_contract::require ((expression), #expression, __LINE__)

constexpr float dpi = 2.0f;

inline juce::Image render (juce::Component& component)
{
    juce::Image image (juce::Image::ARGB, (int) (component.getWidth() * dpi),
                       (int) (component.getHeight() * dpi), true);
    juce::Graphics g (image);
    g.fillAll (BG);
    g.addTransform (juce::AffineTransform::scale (dpi));
    component.paintEntireComponent (g, true);
    return image;
}

// What the arrow adds to a plain button of the same size: its bounds and its width at the top and
// bottom rows it reaches.
struct Ink
{
    juce::Rectangle<int> bounds;
    int topWidth = 0;
    int bottomWidth = 0;
};

inline int rowWidth (const juce::Image& drawn, const juce::Image& bare, int y)
{
    int left = drawn.getWidth(), right = -1;
    for (int x = 0; x < drawn.getWidth(); ++x)
        if (drawn.getPixelAt (x, y) != bare.getPixelAt (x, y))
        {
            left = juce::jmin (left, x);
            right = x;
        }
    return right < left ? 0 : right - left + 1;
}

inline Ink inkOf (const juce::Image& drawn, const juce::Image& bare)
{
    KIRIN_MENU_ARROW_REQUIRE (drawn.getBounds() == bare.getBounds());
    Ink ink;
    for (int y = 0; y < drawn.getHeight(); ++y)
        for (int x = 0; x < drawn.getWidth(); ++x)
            if (drawn.getPixelAt (x, y) != bare.getPixelAt (x, y))
                ink.bounds = ink.bounds.isEmpty() ? juce::Rectangle<int> (x, y, 1, 1)
                                                  : ink.bounds.getUnion ({ x, y, 1, 1 });
    if (! ink.bounds.isEmpty())
    {
        ink.topWidth = rowWidth (drawn, bare, ink.bounds.getY() + 1);
        ink.bottomWidth = rowWidth (drawn, bare, ink.bounds.getBottom() - 2);
    }
    return ink;
}
}

inline void verifyMenuArrowContract()
{
    using namespace menu_arrow_contract;
    int previousWidth = 0;
    for (const auto& preset : observatory::sizePresets)
    {
        if (! observatory::isFullDensity (preset.density))
            continue;
        int width = 0;
        for (const auto role : { observatory::Role::pre, observatory::Role::post })
        {
            observatory::View view (role);
            view.setSize (preset.width, preset.height);
            view.setDomain (observatory::Domain::time);
            auto* menu = dynamic_cast<observatory::Button*> (&view.timeRangeMenuAnchor());
            KIRIN_MENU_ARROW_REQUIRE (menu != nullptr && menu->isVisible());
            KIRIN_MENU_ARROW_REQUIRE (menu->getButtonText().isEmpty());
            KIRIN_MENU_ARROW_REQUIRE (menu->getTitle() == "Choose history time range");
            KIRIN_MENU_ARROW_REQUIRE (menu->getTooltip() == menu->getTitle());

            // The same plate at the same place in the view, under the same key light.
            observatory::Button bare ({}, false);
            bare.setPresentationContext (view.presentationContext());
            view.addChildComponent (bare);
            bare.setBounds (menu->getBounds());
            const auto drawn = render (*menu);
            const auto ink = inkOf (drawn, render (bare));
            view.removeChildComponent (&bare);
            const auto centre = drawn.getBounds().getCentre();
            std::cout << "Menu arrow " << preset.label << ": " << ink.bounds.getWidth() << " x "
                      << ink.bounds.getHeight() << " px at DPI 2, top " << ink.topWidth
                      << ", bottom " << ink.bottomWidth << '\n';
            KIRIN_MENU_ARROW_REQUIRE (ink.bounds.getWidth() >= 12 && ink.bounds.getHeight() >= 7);
            KIRIN_MENU_ARROW_REQUIRE (ink.bounds.getWidth() < drawn.getWidth() / 2
                                      && ink.bounds.getHeight() < drawn.getHeight() / 2);
            // Centred in the button and pointing down: wide where it opens, narrow at the apex.
            KIRIN_MENU_ARROW_REQUIRE (std::abs (ink.bounds.getCentreX() - centre.x) <= 1);
            KIRIN_MENU_ARROW_REQUIRE (std::abs (ink.bounds.getCentreY() - centre.y) <= 2);
            KIRIN_MENU_ARROW_REQUIRE (ink.topWidth > 2 * ink.bottomWidth);
            KIRIN_MENU_ARROW_REQUIRE (width == 0 || width == ink.bounds.getWidth());
            width = ink.bounds.getWidth();
        }
        // The arrow grows with the range label beside it.
        KIRIN_MENU_ARROW_REQUIRE (width > previousWidth);
        previousWidth = width;
    }
}
}
