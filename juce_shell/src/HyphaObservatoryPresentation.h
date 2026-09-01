#pragma once

#include "HyphaObservatoryContract.h"

// Product-level presentation contract for the Observatory shell.
//
// Geometry may still scale through the four persisted size presets, but the product exposes only
// two experiences: a compact meter that prioritises immediate facts, and the full CE2226
// observatory. Keeping this pure makes the breakpoint and world/detail budget executable in native
// render tests without coupling it to JUCE, measurement, or transport state.
namespace hypha::observatory
{
enum class ExperienceFamily
{
    compactMeter,
    observatory,
};

struct PresentationContract
{
    ExperienceFamily family = ExperienceFamily::compactMeter;
    bool worldBackdrop = false;
    bool domainWorld = false;
    bool hyphaAperture = true;
    bool supportingMetrics = false;
    bool detailedAxes = false;
    bool domainTabs = false;
    // Zero means that the Observatory layout is not governed by the Compact two-slot rule.
    unsigned largeMetricSlots = 2;
};

constexpr ExperienceFamily experienceFamily (Density density) noexcept
{
    return density == Density::compact || density == Density::focused
        ? ExperienceFamily::compactMeter : ExperienceFamily::observatory;
}

constexpr ExperienceFamily experienceFamily (SizePreset preset) noexcept
{
    return experienceFamily (preset.density);
}

constexpr PresentationContract presentationContract (SizePreset preset) noexcept
{
    const auto family = experienceFamily (preset);
    const bool full = family == ExperienceFamily::observatory;
    return {
        family,
        full,
        full,
        ! full,
        full,
        full,
        full,
        full ? 0u : 2u,
    };
}

constexpr bool isCompactMeter (SizePreset preset) noexcept
{
    return experienceFamily (preset) == ExperienceFamily::compactMeter;
}

static_assert (experienceFamily (sizePresets[0]) == ExperienceFamily::compactMeter);
static_assert (experienceFamily (sizePresets[1]) == ExperienceFamily::compactMeter);
static_assert (experienceFamily (sizePresets[2]) == ExperienceFamily::observatory);
static_assert (experienceFamily (sizePresets[3]) == ExperienceFamily::observatory);
static_assert (presentationContract (sizePresets[0]).largeMetricSlots == 2u);
static_assert (presentationContract (sizePresets[1]).largeMetricSlots == 2u);
static_assert (presentationContract (sizePresets[2]).largeMetricSlots == 0u);
static_assert (! presentationContract (sizePresets[0]).worldBackdrop);
static_assert (presentationContract (sizePresets[0]).hyphaAperture);
static_assert (presentationContract (sizePresets[3]).worldBackdrop);
static_assert (presentationContract (sizePresets[3]).domainWorld);
static_assert (! presentationContract (sizePresets[3]).hyphaAperture);
}
