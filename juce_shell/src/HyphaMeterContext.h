#pragma once

#include <cstdint>

namespace hypha::meter_context
{
enum class MeterContext : std::uint8_t
{
    trackStem = 0,
    twoMix = 1,
};

enum class ScaleMode : std::uint8_t
{
    wide = 0,
    focus = 1,
};

constexpr MeterContext defaultContext = MeterContext::twoMix;
constexpr ScaleMode defaultScale = ScaleMode::focus;

constexpr std::uint8_t stateValue (MeterContext value) noexcept
{
    return static_cast<std::uint8_t> (value);
}

constexpr std::uint8_t stateValue (ScaleMode value) noexcept
{
    return static_cast<std::uint8_t> (value);
}

constexpr MeterContext contextFromState (std::uint8_t value) noexcept
{
    return value == stateValue (MeterContext::trackStem)
        ? MeterContext::trackStem : MeterContext::twoMix;
}

constexpr ScaleMode scaleFromState (std::uint8_t value) noexcept
{
    return value == stateValue (ScaleMode::wide) ? ScaleMode::wide : ScaleMode::focus;
}

constexpr ScaleMode initialScaleFor (MeterContext value) noexcept
{
    return value == MeterContext::trackStem ? ScaleMode::wide : ScaleMode::focus;
}

constexpr MeterContext nextContext (MeterContext value) noexcept
{
    return value == MeterContext::trackStem ? MeterContext::twoMix : MeterContext::trackStem;
}

constexpr ScaleMode nextScale (ScaleMode value) noexcept
{
    return value == ScaleMode::wide ? ScaleMode::focus : ScaleMode::wide;
}

constexpr double loudnessFloor (ScaleMode value) noexcept
{
    return value == ScaleMode::wide ? -60.0 : -36.0;
}

static_assert (contextFromState (99u) == defaultContext);
static_assert (scaleFromState (99u) == defaultScale);
static_assert (initialScaleFor (MeterContext::trackStem) == ScaleMode::wide);
static_assert (initialScaleFor (MeterContext::twoMix) == ScaleMode::focus);
}
