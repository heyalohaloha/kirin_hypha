#pragma once

#include <cstddef>

namespace hypha::tests
{
// One ordinal is assigned only after the existing case-selection rules accept it.
struct ListenLayoutShard
{
    std::size_t index = 0, count = 1;

    constexpr bool owns (std::size_t ordinal) const noexcept { return ordinal % count == index; }
};
}
