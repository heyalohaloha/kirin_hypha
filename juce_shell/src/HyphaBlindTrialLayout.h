#pragma once

#include <algorithm>

namespace hypha::blind_ui::trial_layout
{
constexpr int sourceMinimum = 60;
constexpr int minimumActionHeight = 24;

struct Plan { int headingPad, actionHeight; bool showStatus; };
struct Fitted { Plan plan; bool roundsInHeading; };

// First reserve the SOURCE buttons and measured prose. All remaining controls
// share one height; extra heading space goes before the touch targets shrink.
constexpr Plan allocate (int height, int sources, int prose, int approval,
                         int heading, int rows, int preferredAction, int headingPad,
                         int optionalStatus = 0) noexcept
{
    const int remaining = height - sources - prose - approval - heading;
    headingPad = std::clamp (remaining - rows * minimumActionHeight, 0, headingPad);
    const bool showStatus = optionalStatus == 0 || remaining - headingPad >= rows * minimumActionHeight;
    const auto available = remaining - headingPad + (showStatus ? 0 : optionalStatus);
    return { headingPad, std::clamp (available / rows, 1, preferredAction), showStatus };
}

// While the count can still be changed, keep the choices reachable by placing
// them beside the title if a separate third control row would be too short.
constexpr Fitted fit (int height, int sources, int prose, int approval, int title, int status,
                      int rows, int preferredAction, int headingPad, int optionalStatus, bool mayShare) noexcept
{
    const auto normal = allocate (height, sources, prose, approval, title + status,
                                   rows, preferredAction, headingPad, optionalStatus);
    if (!mayShare || normal.actionHeight >= minimumActionHeight) return { normal, false };
    const auto shared = allocate (height, sources, prose, approval, std::max (title, preferredAction) + status,
                                   rows - 1, preferredAction, headingPad, optionalStatus);
    return { shared, true };
}

template <class Bounds, class Label, class Button>
void placeRoundChoices (Bounds row, Label& count, Button& one, Button& five, Button& ten)
{
    row = row.withSizeKeepingCentre (std::min (row.getWidth(), 360), row.getHeight());
    // The label has five-pixel side borders and a longer token than 1×/5×/10×.
    count.setBounds (row.removeFromLeft (row.getWidth() * 2 / 7));
    const auto cell = row.getWidth() / 3;
    one.setBounds (row.removeFromLeft (cell).reduced (2, 0));
    five.setBounds (row.removeFromLeft (cell).reduced (2, 0));
    ten.setBounds (row.reduced (2, 0));
}

static_assert (allocate (200, 60, 44, 0, 24, 3, 28, 8).actionHeight == 24);
static_assert (allocate (202, 60, 44, 0, 24, 3, 28, 8).headingPad == 2);
static_assert (fit (200, 60, 62, 0, 16, 18, 3, 29, 4, 18, true).roundsInHeading);
static_assert (fit (200, 60, 62, 0, 16, 18, 3, 29, 4, 18, true).plan.actionHeight == 24);
static_assert (fit (200, 60, 62, 0, 16, 18, 2, 29, 4, 18, false).plan.actionHeight >= 24);
}
