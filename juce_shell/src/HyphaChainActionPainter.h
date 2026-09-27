#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include <juce_graphics/juce_graphics.h>

#include "HyphaTheme.h"
#include "kirin_hypha_chain_observation.h"

namespace hypha::chain_action
{
struct View
{
    const KirinChainSnapshot* snapshot = nullptr;
    const std::vector<KirinChainPoint>* points = nullptr;
    std::uint64_t axisEndObserved = 0;

    bool visible() const noexcept
    {
        return snapshot != nullptr && points != nullptr
            && snapshot->version == KIRIN_CHAIN_VERSION
            && snapshot->sample_rate > 0u && snapshot->count > 0u
            && points->size() == snapshot->count
            && (snapshot->status == KIRIN_CHAIN_ACTIVE
                || snapshot->status == KIRIN_CHAIN_HOLD);
    }
};

inline std::optional<float> xFor (juce::Rectangle<float> area,
                                  const View& view,
                                  const KirinChainPoint& point) noexcept
{
    if (! view.visible() || point.post_observed > view.axisEndObserved)
        return std::nullopt;
    const auto window = static_cast<std::uint64_t> (view.snapshot->sample_rate) * 60u;
    const auto age = view.axisEndObserved - point.post_observed;
    if (window == 0u || age > window)
        return std::nullopt;
    return area.getX() + static_cast<float> (
        1.0 - static_cast<double> (age) / static_cast<double> (window)) * area.getWidth();
}

/// Cached paths for one immutable chain revision and one 60-second geometry. The cache is
/// owned by the view, not process-global; Capture gets its own frozen render instance.
class GeometryCache
{
public:
    void invalidate() noexcept { valid = false; }

    void update (juce::Rectangle<float> area, View view)
    {
        if (! view.visible())
        {
            invalidate();
            return;
        }
        if (valid && revision == view.snapshot->revision && binding == view.snapshot->binding
            && axisEnd == view.axisEndObserved && sampleRate == view.snapshot->sample_rate
            && bounds == area)
            return;
        valid = true;
        revision = view.snapshot->revision;
        binding = view.snapshot->binding;
        axisEnd = view.axisEndObserved;
        sampleRate = view.snapshot->sample_rate;
        bounds = area;
        relation.clear();
        relationUp.clear();
        relationDown.clear();
        preModerate.clear();
        preStrong.clear();
        postModerate.clear();
        postStrong.clear();
        const auto capacity = static_cast<int> (view.points->size() * 6u);
        relation.preallocateSpace (capacity);
        for (auto* path : { &preModerate, &preStrong, &postModerate, &postStrong })
            path->preallocateSpace (capacity);

        const auto relationArea = relationBounds (area);
        const auto preY = area.getBottom() - 8.5f;
        const auto postY = area.getBottom() - 2.5f;
        bool relationOpen = false;
        Previous previous;
        for (const auto& point : *view.points)
        {
            const auto x = xFor (area, view, point);
            if (! x)
            {
                relationOpen = false;
                previous = {};
                continue;
            }
            const bool continuous = previous.valid
                && previous.preRun == point.pre_run
                && previous.postRun == point.post_run
                && previous.preGeneration == point.pre_generation
                && previous.postGeneration == point.post_generation
                && point.post_observed > previous.postObserved
                && point.post_observed - previous.postObserved
                    == static_cast<std::uint64_t> (view.snapshot->sample_rate / 10u);
            if (std::isfinite (point.relation))
            {
                const auto clamped = juce::jlimit (-12.0, 12.0, point.relation);
                const auto y = relationArea.getCentreY()
                    - static_cast<float> (clamped / 12.0) * relationArea.getHeight() * 0.46f;
                if (! continuous || ! relationOpen)
                    relation.startNewSubPath (*x, y);
                else
                    relation.lineTo (*x, y);
                relationOpen = true;
                if (point.relation > 12.0)
                    overflow (relationUp, *x, relationArea.getY() + 1.0f, true);
                else if (point.relation < -12.0)
                    overflow (relationDown, *x, relationArea.getBottom() - 1.0f, false);
            }
            else
                relationOpen = false;
            addBand (preModerate, preStrong, point.pre_severity, previous.preSeverity,
                     continuous, *x, preY);
            addBand (postModerate, postStrong, point.post_severity, previous.postSeverity,
                     continuous, *x, postY);
            previous = { true, point.pre_run, point.post_run, point.pre_generation,
                         point.post_generation, point.post_observed,
                         point.pre_severity, point.post_severity };
        }
    }

    void paint (juce::Graphics& g) const
    {
        if (! valid)
            return;
        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (bounds.toNearestInt());
        const auto rel = relationBounds (bounds);
        g.setColour (COL_MUTED.withAlpha (0.40f));
        g.drawHorizontalLine (juce::roundToInt (rel.getCentreY()), rel.getX(), rel.getRight());
        g.setColour (COL_FLORA_BR.withAlpha (0.84f));
        g.strokePath (relation, juce::PathStrokeType (1.15f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillPath (relationUp);
        g.fillPath (relationDown);
        paintBand (g, preModerate, preStrong, COL_LED_BLUE);
        paintBand (g, postModerate, postStrong, COL_FLORA_BR);
    }

private:
    struct Previous
    {
        bool valid = false;
        std::uint64_t preRun = 0, postRun = 0;
        std::uint64_t preGeneration = 0, postGeneration = 0, postObserved = 0;
        std::uint8_t preSeverity = 0, postSeverity = 0;
    };

    static juce::Rectangle<float> relationBounds (juce::Rectangle<float> area) noexcept
    {
        return area.withBottom (area.getBottom() - 12.0f);
    }

    static void addBand (juce::Path& moderate, juce::Path& strong,
                         std::uint8_t severity, std::uint8_t previousSeverity,
                         bool continuous, float x, float y)
    {
        if (severity < 2u)
            return;
        auto& path = severity == 3u ? strong : moderate;
        if (continuous && previousSeverity == severity)
            path.lineTo (x, y);
        else
        {
            path.startNewSubPath (x - 1.2f, y);
            path.lineTo (x + 1.2f, y);
        }
    }

    static void overflow (juce::Path& path, float x, float y, bool up)
    {
        const auto direction = up ? 1.0f : -1.0f;
        path.startNewSubPath (x, y);
        path.lineTo (x - 2.2f, y + direction * 3.0f);
        path.lineTo (x + 2.2f, y + direction * 3.0f);
        path.closeSubPath();
    }

    static void paintBand (juce::Graphics& g, const juce::Path& moderate,
                           const juce::Path& strong, juce::Colour colour)
    {
        g.setColour (colour.withAlpha (0.68f));
        g.strokePath (moderate, juce::PathStrokeType (1.5f));
        g.setColour (colour.withAlpha (0.18f));
        g.strokePath (strong, juce::PathStrokeType (5.0f));
        g.setColour (colour.withAlpha (0.96f));
        g.strokePath (strong, juce::PathStrokeType (2.5f));
    }

    bool valid = false;
    std::uint64_t revision = 0, binding = 0, axisEnd = 0;
    std::uint32_t sampleRate = 0;
    juce::Rectangle<float> bounds;
    juce::Path relation, relationUp, relationDown;
    juce::Path preModerate, preStrong, postModerate, postStrong;
};
}
