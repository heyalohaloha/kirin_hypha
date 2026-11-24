#pragma once

#include <algorithm>

namespace hypha::blind_ui::trial_layout
{
constexpr int sourceMinimum = 60;
constexpr int minimumActionHeight = 24;

constexpr int actionHeightFor (int remaining, int rows, int preferred) noexcept
{
    return std::clamp (minimumActionHeight + std::max (0, remaining) / std::max (1, rows),
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

constexpr int edgeSide = 24;
// The quiet frame line runs 10 px inside the editor, and the result page starts its title at edgeInset.
// Rows stand inside the frame line at the sides wherever the window has the width for it.
constexpr int sideInset (int width) noexcept
{
    return width >= 450 ? std::clamp (width / 25, 18, edgeSide) : std::clamp (width / 40, 6, edgeSide);
}
constexpr int frameClearance = 12, edgeInset = 24, resultTitleHeight = 40, reservedRows = 3;

// The height the rows of a trial do not need. The title clears the frame line first; then the two edges, the
// gaps between the rows, the title and the SOURCE row take a fixed measure of it. Whatever remains stays between
// the guidance and the actions, so SOURCE keeps its place at the top and END at the bottom in every round.
struct Spacing { int top = 0, bottom = 0, gap = 0, title = 0, source = 0, lead = 0; };

constexpr Spacing spacingFor (int spare, int height, int rows, int titleHeight, bool answersOnly = false) noexcept
{
    Spacing s;
    const auto take = [&spare] (int wanted)
    {
        const int given = std::clamp (wanted, 0, std::max (0, spare));
        spare -= given;
        return given;
    };
    s.top = take (frameClearance);
    s.bottom = take (frameClearance);
    s.top += take (edgeInset - frameClearance);
    s.bottom += take (edgeInset - frameClearance);
    s.gap = std::min (std::clamp ((height - 240) / 16, 6, 22), std::max (0, spare) / (rows + 2));
    spare -= s.gap * (rows + 2);
    if (titleHeight > 0) s.title = take (resultTitleHeight - titleHeight);
    s.source = take ((answersOnly ? std::max (sourceMinimum, height * 2 / 5)
                                  : std::max (sourceMinimum, height >= 500 ? height * 3 / 10 : height / 4)) - sourceMinimum);
    if (answersOnly) s.lead = take (std::max (0, spare) / 2);
    return s;
}

static_assert (spacingFor (0, 200, 3, 20).top == 0);
static_assert (spacingFor (20, 200, 3, 20).top == frameClearance);
static_assert (spacingFor (400, 600, 3, 22).top == edgeInset && spacingFor (400, 600, 3, 22).source == 120);
static_assert (actionHeightFor (0, 2, 29) == 24);
static_assert (actionHeightFor (10, 2, 29) == 29);
}
