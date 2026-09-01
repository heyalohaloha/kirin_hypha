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

// Optional large analysis is intentionally bounded to two concurrent owners. This supports
// track-to-track comparison and one 2MIX plus one track without making every always-on Compact
// instance pay the analysis cost.
constexpr unsigned maximumConcurrentObservatorySlots = 2u;

struct PresentationContract
{
    ExperienceFamily family = ExperienceFamily::compactMeter;
    bool worldBackdrop = false;
    bool domainWorld = false;
    bool hyphaAperture = true;
    bool supportingMetrics = false;
    bool detailedAxes = false;
    bool domainTabs = false;
    // Zero means the full Observatory information hierarchy is not governed by this Compact cap.
    unsigned maximumNumericFacts = 3;
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
    const bool detailed = family == ExperienceFamily::observatory;
    const bool full = preset.density == Density::observatory;
    return {
        family,
        full,
        full,
        ! full,
        detailed,
        detailed,
        detailed,
        detailed ? 0u : 3u,
    };
}

constexpr bool isCompactMeter (SizePreset preset) noexcept
{
    return experienceFamily (preset) == ExperienceFamily::compactMeter;
}

constexpr bool captureEntryAvailable (Role role, SizePreset preset) noexcept
{
    return role == Role::post && preset.density == Density::observatory;
}

static_assert (experienceFamily (sizePresets[0]) == ExperienceFamily::compactMeter);
static_assert (experienceFamily (sizePresets[1]) == ExperienceFamily::compactMeter);
static_assert (experienceFamily (sizePresets[2]) == ExperienceFamily::observatory);
static_assert (experienceFamily (sizePresets[3]) == ExperienceFamily::observatory);
static_assert (maximumConcurrentObservatorySlots == 2u);
static_assert (presentationContract (sizePresets[0]).maximumNumericFacts == 3u);
static_assert (presentationContract (sizePresets[1]).maximumNumericFacts == 3u);
static_assert (presentationContract (sizePresets[2]).maximumNumericFacts == 0u);
static_assert (! presentationContract (sizePresets[0]).worldBackdrop);
static_assert (presentationContract (sizePresets[0]).hyphaAperture);
static_assert (! presentationContract (sizePresets[2]).worldBackdrop);
static_assert (presentationContract (sizePresets[2]).hyphaAperture);
static_assert (presentationContract (sizePresets[3]).worldBackdrop);
static_assert (presentationContract (sizePresets[3]).domainWorld);
static_assert (! presentationContract (sizePresets[3]).hyphaAperture);
static_assert (! captureEntryAvailable (Role::post, sizePresets[0]));
static_assert (! captureEntryAvailable (Role::post, sizePresets[1]));
static_assert (! captureEntryAvailable (Role::post, sizePresets[2]));
static_assert (captureEntryAvailable (Role::post, sizePresets[3]));
static_assert (! captureEntryAvailable (Role::pre, sizePresets[3]));
}
