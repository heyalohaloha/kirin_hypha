#pragma once

#include <algorithm>

namespace hypha::blind_ui::trial_layout
{
constexpr int sourceMinimum = 60;
constexpr int minimumActionHeight = 24;

struct Plan { int headingPad, actionHeight; bool showStatus; };

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

static_assert (allocate (200, 60, 44, 0, 24, 3, 28, 8).actionHeight == 24);
static_assert (allocate (202, 60, 44, 0, 24, 3, 28, 8).headingPad == 2);
}
