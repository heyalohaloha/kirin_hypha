#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace hypha::attack_ui_test
{
inline bool nearColour (juce::Colour pixel, juce::Colour target, int tolerance = 12)
{
    return pixel.getAlpha() > 16
        && std::abs ((int) pixel.getRed() - (int) target.getRed()) <= tolerance
        && std::abs ((int) pixel.getGreen() - (int) target.getGreen()) <= tolerance
        && std::abs ((int) pixel.getBlue() - (int) target.getBlue()) <= tolerance;
}

inline juce::Rectangle<int> rectangle (attack_ui::Box box)
{
    return { box.x, box.y, box.width, box.height };
}

// Mirrors AttackComponent::plotColumn: every row shares one horizontal plot column.
inline juce::Rectangle<int> plotColumn (const attack_ui::Layout& layout, attack_ui::Box row)
{
    auto column = rectangle (row);
    if (layout.arrangement == attack_ui::Arrangement::lanes)
    {
        column.removeFromLeft (layout.labelWidth);
        column.removeFromRight (layout.readoutWidth);
    }
    return column.reduced (1, 0);
}

inline juce::Rectangle<int> historyPlot (const attack_ui::Layout& layout)
{
    return plotColumn (layout, layout.history).reduced (0, 1);
}

inline juce::Rectangle<int> historyReadout (const attack_ui::Layout& layout)
{
    return rectangle (layout.history).removeFromRight (layout.readoutWidth);
}

inline juce::Rectangle<int> lanesArea (const attack_ui::Layout& layout)
{
    const auto first = rectangle (layout.lanes.front());
    return first.withBottom (layout.lanes.back().bottom());
}

inline int differences (const juce::Image& first, const juce::Image& second,
                        juce::Rectangle<int> requested = {})
{
    if (first.getBounds() != second.getBounds()) return -1;
    const auto area = requested.isEmpty() ? first.getBounds()
                                          : requested.getIntersection (first.getBounds());
    int count = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            count += first.getPixelAt (x, y) != second.getPixelAt (x, y);
    return count;
}

inline std::uint64_t light (const juce::Image& image, juce::Rectangle<int> requested = {})
{
    const auto area = requested.isEmpty() ? image.getBounds()
                                           : requested.getIntersection (image.getBounds());
    std::uint64_t total = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto pixel = image.getPixelAt (x, y);
            total += static_cast<std::uint64_t> (pixel.getAlpha())
                   * static_cast<std::uint64_t> (pixel.getPerceivedBrightness() * 1'000.0f);
        }
    return total;
}

inline int countColour (const juce::Image& image, juce::Rectangle<int> requested,
                        juce::Colour target, int tolerance = 12)
{
    const auto area = requested.getIntersection (image.getBounds());
    int count = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            count += nearColour (image.getPixelAt (x, y), target, tolerance);
    return count;
}

inline juce::Image renderAttack (juce::Component& component)
{
    juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics graphics (image);
    component.paintEntireComponent (graphics, true);
    return image;
}

inline bool writePreviewTo (const char* environmentName, const juce::Image& image)
{
    const auto* path = std::getenv (environmentName);
    if (path == nullptr)
        return true;
    juce::FileOutputStream output { juce::File { path } };
    juce::PNGImageFormat png;
    return output.openedOk() && png.writeImageToStream (image, output);
}
}
