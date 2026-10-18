#pragma once

#include <cstddef>
#include <vector>

// Which numbers an axis names when its marks stand closer than a label's box (2026-10-18:
// in the saved 1200x630 image the LEVEL history and TP scales overlapped). Labels are taken in the
// order given (the most useful first: the ends and 0); one whose box would overlap a label already
// taken is left out. The marks' lines are still drawn; only their numbers thin out.
namespace hypha::axis_labels
{
// centres: each mark's label centre (y); order: indices into centres, most useful first.
inline std::vector<bool> named (const std::vector<float>& centres, const std::vector<std::size_t>& order,
                                float boxHeight)
{
    std::vector<bool> keep (centres.size(), false);
    std::vector<float> taken;
    for (const auto index : order)
    {
        if (index >= centres.size() || keep[index]) continue;
        bool clear = true;
        for (const auto other : taken)
            clear = clear && (centres[index] - other >= boxHeight || other - centres[index] >= boxHeight);
        if (! clear) continue;
        keep[index] = true;
        taken.push_back (centres[index]);
    }
    return keep;
}

// The usual order: first, last, middle, then the rest from the top.
inline std::vector<std::size_t> endsFirst (std::size_t count)
{
    std::vector<std::size_t> order;
    if (count == 0) return order;
    order.push_back (0);
    if (count > 1) order.push_back (count - 1);
    if (count > 2) order.push_back (count / 2);
    for (std::size_t index = 1; index + 1 < count; ++index)
        if (index != count / 2) order.push_back (index);
    return order;
}
}
