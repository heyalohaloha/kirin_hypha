#pragma once

#include <array>

#include "HyphaPresentationContext.h"

namespace hypha::typography
{
enum class TextRole
{
    shellTitle,
    navigation,
    primaryValue,
    secondaryValue,
    metricLabel,
    unit,
    axis,
    legend,
    readout,
    sectionTitle,
    body,
    status,
    action,
    selector,
    menu,
    tooltip,
    captureMetadata,
};

enum class Composition
{
    shell,
    facts,
    visualization,
    information,
    instrument,
};

enum class Overflow
{
    preserve,
    ellipsize,
    wrap,
};

struct TextStyle
{
    float fontHeight = 11.0f;
    float lineHeight = 13.0f;
    float horizontalPadding = 3.0f;
    Overflow overflow = Overflow::preserve;
};

using Scale = std::array<float, 5>;

constexpr Scale roleScale (TextRole role, Composition composition) noexcept
{
    switch (role)
    {
        case TextRole::shellTitle: return { 12.0f, 14.0f, 16.0f, 18.0f, 23.0f };
        case TextRole::navigation: return { 11.0f, 11.5f, 12.0f, 13.0f, 16.0f };
        case TextRole::primaryValue:
            switch (composition)
            {
                case Composition::facts: return { 25.0f, 27.0f, 30.0f, 42.0f, 58.0f };
                case Composition::visualization: return { 14.0f, 15.0f, 18.0f, 22.0f, 29.0f };
                case Composition::information: return { 15.0f, 16.0f, 19.0f, 24.0f, 32.0f };
                case Composition::instrument: return { 18.0f, 20.0f, 24.0f, 32.0f, 44.0f };
                case Composition::shell: return { 14.0f, 15.0f, 18.0f, 22.0f, 29.0f };
            }
            break;
        case TextRole::secondaryValue:
            switch (composition)
            {
                case Composition::facts: return { 14.0f, 15.0f, 18.0f, 23.0f, 30.0f };
                case Composition::visualization: return { 12.0f, 13.0f, 15.0f, 18.0f, 23.0f };
                case Composition::information: return { 12.0f, 13.0f, 16.0f, 19.0f, 25.0f };
                case Composition::instrument: return { 14.0f, 15.0f, 18.0f, 23.0f, 30.0f };
                case Composition::shell: return { 12.0f, 13.0f, 15.0f, 18.0f, 23.0f };
            }
            break;
        case TextRole::metricLabel: return { 11.0f, 11.0f, 11.5f, 13.0f, 16.0f };
        case TextRole::unit: return { 11.0f, 11.0f, 11.0f, 12.0f, 14.0f };
        case TextRole::axis: return { 11.0f, 11.0f, 11.0f, 12.0f, 14.0f };
        case TextRole::legend: return { 11.0f, 11.0f, 11.5f, 13.0f, 16.0f };
        case TextRole::readout: return { 11.0f, 11.0f, 12.0f, 14.0f, 17.0f };
        case TextRole::sectionTitle: return { 12.0f, 13.0f, 15.0f, 18.0f, 23.0f };
        case TextRole::body: return { 11.0f, 11.5f, 12.5f, 14.0f, 17.0f };
        case TextRole::status: return { 11.0f, 12.0f, 14.0f, 17.0f, 22.0f };
        case TextRole::action: return { 11.0f, 12.0f, 13.0f, 15.0f, 18.0f };
        case TextRole::selector: return { 11.0f, 12.0f, 13.0f, 15.0f, 18.0f };
        case TextRole::menu: return { 16.0f, 16.0f, 16.0f, 16.0f, 16.0f };
        case TextRole::tooltip: return { 11.5f, 11.5f, 11.5f, 11.5f, 11.5f };
        case TextRole::captureMetadata: return { 11.0f, 11.0f, 11.0f, 12.0f, 14.0f };
    }
    return { 11.0f, 11.0f, 11.0f, 11.0f, 11.0f };
}

constexpr float interpolate (const Scale& values, float position) noexcept
{
    if (position <= 0.0f) return values.front();
    if (position >= 4.0f) return values.back();
    const auto left = static_cast<int> (position);
    const auto fraction = position - static_cast<float> (left);
    return values[static_cast<size_t> (left)]
         + (values[static_cast<size_t> (left + 1)] - values[static_cast<size_t> (left)])
           * fraction;
}

constexpr Overflow overflowFor (TextRole role) noexcept
{
    switch (role)
    {
        case TextRole::body:
        case TextRole::status:
        case TextRole::tooltip: return Overflow::wrap;
        case TextRole::selector:
        case TextRole::menu:
        case TextRole::captureMetadata: return Overflow::ellipsize;
        case TextRole::shellTitle:
        case TextRole::navigation:
        case TextRole::primaryValue:
        case TextRole::secondaryValue:
        case TextRole::metricLabel:
        case TextRole::unit:
        case TextRole::axis:
        case TextRole::legend:
        case TextRole::readout:
        case TextRole::sectionTitle:
        case TextRole::action:
            return Overflow::preserve;
    }
    return Overflow::preserve;
}

constexpr TextStyle resolve (const presentation::Context& context,
                             TextRole role,
                             Composition composition = Composition::shell) noexcept
{
    auto fontHeight = interpolate (roleScale (role, composition),
                                   presentation::densityPosition (context));
    if (context.output == presentation::OutputTarget::popup)
        fontHeight = roleScale (TextRole::menu, Composition::shell).front();
    else if (context.output == presentation::OutputTarget::tooltip)
        fontHeight = roleScale (TextRole::tooltip, Composition::shell).front();

    const auto numeric = role == TextRole::primaryValue || role == TextRole::secondaryValue
                      || role == TextRole::readout || role == TextRole::axis;
    const auto lineMultiplier = numeric ? 1.12f : role == TextRole::body ? 1.32f : 1.20f;
    const auto padding = role == TextRole::action || role == TextRole::selector
        ? fontHeight * 0.55f : fontHeight * 0.25f;
    return { fontHeight, fontHeight * lineMultiplier, padding, overflowFor (role) };
}

constexpr auto compact = presentation::forEditor (300, 200);
constexpr auto standard = presentation::forEditor (450, 300);
constexpr auto inspection = presentation::forEditor (900, 600);
static_assert (resolve (compact, TextRole::axis).fontHeight >= 11.0f);
static_assert (resolve (compact, TextRole::primaryValue, Composition::facts).fontHeight
               > resolve (compact, TextRole::metricLabel).fontHeight);
static_assert (resolve (inspection, TextRole::primaryValue, Composition::facts).fontHeight
               > resolve (standard, TextRole::primaryValue, Composition::facts).fontHeight);
static_assert (resolve (inspection, TextRole::axis).fontHeight
               > resolve (compact, TextRole::axis).fontHeight);
static_assert (resolve (compact, TextRole::body).overflow == Overflow::wrap);
}
