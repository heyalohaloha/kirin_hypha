#pragma once

#include <cmath>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"

// The editor's one key light (2026-10-01, stages 2 and 3 of the light,
// docs/planning/hypha_lighting_completion_20261005.md): above the editor and a little left of its
// centre. Each page's main window catches it on its frame, and each control plate on its upper
// bevel, where they come nearest to it, the brighter the nearer, so no two are lit alike and
// nothing shows a light that is not there.
namespace hypha::key_light
{
// The component whose coordinates are the editor's logical coordinates: the editor's scale root.
inline const juce::Identifier rootProperty { "kirinHyphaLightRoot" };

// The light as a painter sees it: where it is in the painter's coordinates, and the editor's
// diagonal, the distance over which it fades.
struct Light
{
    juce::Point<float> position;
    float diagonal = 1.0f;

    bool operator== (const Light& other) const noexcept
    {
        return position == other.position && std::equal_to<float> {} (diagonal, other.diagonal);
    }
};

// The light over an editor of `width` x `height` logical points, in the editor's coordinates.
inline Light overEditor (float width, float height) noexcept
{
    return { { 0.42f * width, -0.6f * height }, std::hypot (width, height) };
}

inline Light inEditor (const presentation::Context& context) noexcept
{
    return overEditor (static_cast<float> (context.logicalWidth), static_cast<float> (context.logicalHeight));
}

namespace detail
{
struct Coordinates
{
    const juce::Component* root = nullptr;
    Light light;
};

inline Coordinates& coordinates() noexcept
{
    thread_local Coordinates value;
    return value;
}
}

// Capture paints a live pane at another editor size without reparenting it. Its root light is
// expressed at the pane's Capture origin; nested child painters inherit that temporary root.
class CoordinateScope
{
public:
    CoordinateScope (const juce::Component& root, const presentation::Context& context,
                     juce::Point<float> origin) noexcept : previous (detail::coordinates())
    {
        auto light = inEditor (context);
        light.position -= origin;
        detail::coordinates() = { &root, light };
    }
    ~CoordinateScope() { detail::coordinates() = previous; }
    CoordinateScope (const CoordinateScope&) = delete;
    CoordinateScope& operator= (const CoordinateScope&) = delete;

private:
    detail::Coordinates previous;
};

// The light in `component`'s coordinates: the editor is its scale root, or the top-most parent
// when the component is drawn on its own. CoordinateScope overrides only its pane's subtree.
inline Light forComponent (const juce::Component& component)
{
    const auto& coordinates = detail::coordinates();
    const auto* item = &component;
    while (item->getParentComponent() != nullptr
           && item != coordinates.root
           && ! static_cast<bool> (item->getProperties().getWithDefault (rootProperty, false)))
        item = item->getParentComponent();
    auto light = item == coordinates.root ? coordinates.light
        : overEditor (static_cast<float> (item->getWidth()), static_cast<float> (item->getHeight()));
    // JUCE's coordinate conversion accounts for transforms below the logical root; its own
    // magnification is outside this conversion and therefore never changes the logical light.
    const auto start = component.getLocalPoint (item, juce::Point<float> {});
    const auto end = component.getLocalPoint (item, juce::Point<float> { light.diagonal, 0.0f });
    light.position = component.getLocalPoint (item, light.position);
    light.diagonal = start.getDistanceFrom (end);
    return light;
}

namespace detail
{
struct Current
{
    Light light = inEditor (presentation::defaultContext());
    bool active = false;
};

inline Current& current() noexcept
{
    thread_local Current value;
    return value;
}
}

// While a component paints, the light in its own coordinates; the painters below it read it.
// Painting runs one component at a time on its thread.
class Scope
{
public:
    explicit Scope (const juce::Component& component) : previous (detail::current())
    {
        detail::current() = { forComponent (component), true };
    }
    ~Scope() { detail::current() = previous; }
    Scope (const Scope&) = delete;
    Scope& operator= (const Scope&) = delete;

private:
    detail::Current previous;
};

// The light of the component now painting (a 100% editor's light outside any scope).
inline const Light& current() noexcept
{
    return detail::current().light;
}

// Whether a component is painting inside a scope: a control plate drawn outside one keeps the
// plain light from the upper left.
inline bool active() noexcept
{
    return detail::current().active;
}

// Where the light lands on an edge (0 at its left end, 1 at its right) and how much of it reaches
// it (0.35 far to 1 near), in steps fine enough to see and coarse enough to cache.
struct Landing
{
    float fraction = 0.5f;
    float strength = 1.0f;
};

inline Landing landingOn (juce::Rectangle<float> frame, const Light& light) noexcept
{
    if (frame.isEmpty())
        return {};
    const auto x = juce::jlimit (frame.getX(), frame.getRight(), light.position.x);
    const auto distance = juce::Point<float> (x, frame.getY()).getDistanceFrom (light.position);
    const auto step = [] (float value) { return std::round (value * 256.0f) / 256.0f; };
    return { step ((x - frame.getX()) / frame.getWidth()),
             step (juce::jlimit (0.35f, 1.0f, 1.3f - distance / juce::jmax (1.0f, light.diagonal))) };
}
}
