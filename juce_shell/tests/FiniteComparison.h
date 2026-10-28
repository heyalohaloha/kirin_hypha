#pragma once
#include <cmath>
namespace hypha::tests
{
template <typename T> bool finiteNear (T actual, T expected, T tolerance)
{
    return std::isfinite (actual) && std::isfinite (expected) && std::isfinite (tolerance)
        && tolerance >= T{} && std::abs (actual - expected) <= tolerance;
}
}
