#pragma once

#include "../src/HyphaKeyLight.h"
#include "CompactReviewShowcase.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
namespace key_light_coordinates
{
inline void require (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "Key light coordinate contract: " << message << '\n';
        std::exit (EXIT_FAILURE);
    }
}

inline bool close (float a, float b) noexcept { return std::abs (a - b) < 0.001f; }

inline void expect (const key_light::Light& actual, juce::Point<float> position, float diagonal,
                    const char* message)
{
    require (close (actual.position.x, position.x) && close (actual.position.y, position.y)
             && close (actual.diagonal, diagonal), message);
}

inline void verifyCaptureRootAndRestoration()
{
    juce::Component host, root, pane, child, other;
    host.setSize (1'800, 1'200);
    host.addChildComponent (root);
    root.setBounds (20, 40, 900, 600);
    root.setTransform (juce::AffineTransform::scale (2.0f));
    root.getProperties().set (key_light::rootProperty, true);
    root.addChildComponent (pane);
    root.addChildComponent (other);
    pane.setBounds (30, 100, 600, 360);
    pane.addChildComponent (child);
    child.setBounds (40, 20, 80, 24);
    other.setBounds (700, 100, 120, 24);
    const auto editor = key_light::overEditor (900.0f, 600.0f);
    expect (key_light::forComponent (pane), { 348.0f, -460.0f }, editor.diagonal,
            "root magnification never changes the logical editor light");
    const auto savedBounds = pane.getBounds();
    const auto savedTransform = root.getTransform();
    const auto savedCurrent = key_light::current();
    const auto savedActive = key_light::active();
    const auto capture = presentation::forOutput (300, 200, presentation::OutputTarget::capture);
    const auto captureDiagonal = key_light::inEditor (capture).diagonal;
    {
        const key_light::Scope editorScope (root);
        pane.setSize (240, 120); // Capture resizes the live pane but retains its real parent.
        {
            const key_light::CoordinateScope coordinates (pane, capture, { 10.0f, 60.0f });
            expect (key_light::forComponent (pane), { 116.0f, -180.0f }, captureDiagonal,
                    "Capture uses its logical editor dimensions and body origin");
            expect (key_light::forComponent (child), { 76.0f, -200.0f }, captureDiagonal,
                    "Capture child controls inherit the same coordinate root");
            expect (key_light::forComponent (other), { -322.0f, -460.0f }, editor.diagonal,
                    "a Capture override never affects an unrelated sibling");
            {
                const key_light::Scope paneScope (pane);
                const auto parentLight = key_light::current();
                {
                    const key_light::Scope childScope (child);
                    expect (key_light::current(), { 76.0f, -200.0f }, captureDiagonal,
                            "nested paint Scope uses the Capture light");
                }
                require (key_light::current() == parentLight, "child Scope restores its parent light");
                {
                    const key_light::CoordinateScope nested (pane,
                        presentation::forOutput (600, 400, presentation::OutputTarget::capture),
                        { 20.0f, 80.0f });
                    expect (key_light::forComponent (pane), { 232.0f, -320.0f },
                            std::hypot (600.0f, 400.0f), "nested Capture uses its own coordinates");
                }
                expect (key_light::forComponent (pane), { 116.0f, -180.0f }, captureDiagonal,
                        "nested CoordinateScope restores the previous Capture root");
            }
            require (key_light::current() == editor, "pane Scope restores the live editor light");
        }
        expect (key_light::forComponent (pane), { 348.0f, -460.0f }, editor.diagonal,
                "Capture exit restores the live parent coordinate system");
        pane.setBounds (savedBounds);
    }
    require (key_light::current() == savedCurrent && key_light::active() == savedActive,
             "all paint scopes restore their previous state");
    require (pane.getBounds() == savedBounds && pane.getParentComponent() == &root
             && root.getTransform() == savedTransform, "Capture retains bounds, parent and magnification");

    child.setTransform (juce::AffineTransform::scale (2.0f));
    const auto transformed = key_light::forComponent (child);
    // JUCE scales the child's parent-space position as well as its contents.
    expect (transformed, { 134.0f, -250.0f }, editor.diagonal * 0.5f,
            "a transformed child receives the light and distance scale in its own coordinates");
}

inline int differences (const juce::Image& a, const juce::Image& b)
{
    int count = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y)) ++count;
    return count;
}

inline void verifyDrumCacheUsesTheWholeLight()
{
    auto drum = compact_review::drum();
    drum->setPresentationContext (presentation::forEditor (900, 600));
    drum->setSize (700, 340);
    drum->presentationTickAt (60'000.0);
    const auto render = [&drum] (presentation::Context context, juce::Point<float> origin) {
        const key_light::CoordinateScope coordinates (*drum, context, origin);
        return drum->createComponentSnapshot (drum->getLocalBounds(), true, 1.0f);
    };
    const auto small = presentation::forOutput (300, 200, presentation::OutputTarget::capture);
    const auto large = presentation::forOutput (900, 600, presentation::OutputTarget::capture);
    // Both roots put the light at (150,-200) in this pane. Only the editor diagonal changes,
    // which changes the frame's thickness and attenuation; a position-only cache key is wrong.
    const auto before = render (small, { -24.0f, 80.0f });
    require (drum->cachedChromeBytes() > 0u, "DRUM fixture builds its steady chrome cache");
    const auto changed = render (large, { 228.0f, -160.0f });
    drum->releaseCachedChrome();
    const auto fresh = render (large, { 228.0f, -160.0f });
    require (differences (before, fresh) > 100, "different editor diagonals visibly change DRUM's frame");
    require (differences (changed, fresh) == 0, "DRUM cache invalidates when only the light diagonal changes");
}
}

inline void verifyKeyLightCoordinateContract()
{
    key_light_coordinates::verifyCaptureRootAndRestoration();
    key_light_coordinates::verifyDrumCacheUsesTheWholeLight();
}
}
