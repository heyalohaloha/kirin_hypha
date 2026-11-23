#pragma once

#include <algorithm>

namespace hypha::blind_ui::trial_layout
{
constexpr int sourceMinimum = 60;
constexpr int minimumActionHeight = 24;

constexpr int actionHeightFor (int remaining, int rows, int preferred) noexcept
{
    return std::clamp (minimumActionHeight + std::max (0, remaining) / rows,
                       minimumActionHeight, preferred);
}

template <class Bounds, class Label, class Button>
void placeRoundChoices (Bounds row, Label& count, Button& one, Button& five, Button& ten)
{
    row = row.withSizeKeepingCentre (std::min (row.getWidth(), 360), row.getHeight());
    // The label has five-pixel side borders and a longer token than 1×/5×/10×.
    count.setBounds (row.removeFromLeft (std::min (row.getWidth(), std::max (56, row.getWidth() * 2 / 7))));
    const auto cell = row.getWidth() / 3;
    one.setBounds (row.removeFromLeft (cell).reduced (2, 0));
    five.setBounds (row.removeFromLeft (cell).reduced (2, 0));
    ten.setBounds (row.reduced (2, 0));
}

static_assert (actionHeightFor (0, 2, 29) == 24);
static_assert (actionHeightFor (10, 2, 29) == 29);
}
