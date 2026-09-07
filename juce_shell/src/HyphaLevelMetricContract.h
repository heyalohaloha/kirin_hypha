#pragma once

#include <array>
#include <cstddef>

namespace hypha::level_metrics
{
enum class Metric
{
    momentary,
    shortTerm,
    integrated,
    crest,
    psr,
    truePeak,
    maximumTruePeak,
    loudnessRange,
    plr,
};

struct Layout
{
    std::array<Metric, 3> main;
    std::array<Metric, 5> support;
};

constexpr Layout layoutFor (bool trackStem) noexcept
{
    return trackStem
        ? Layout {
            { Metric::momentary, Metric::shortTerm, Metric::crest },
            { Metric::psr, Metric::truePeak, Metric::maximumTruePeak,
              Metric::integrated, Metric::loudnessRange }
          }
        : Layout {
            { Metric::momentary, Metric::shortTerm, Metric::integrated },
            { Metric::truePeak, Metric::maximumTruePeak, Metric::loudnessRange,
              Metric::plr, Metric::crest }
          };
}

constexpr const char* label (Metric metric) noexcept
{
    switch (metric)
    {
        case Metric::momentary:       return "M";
        case Metric::shortTerm:       return "S";
        case Metric::integrated:      return "I";
        case Metric::crest:           return "CREST";
        case Metric::psr:             return "PSR";
        case Metric::truePeak:        return "TP";
        case Metric::maximumTruePeak: return "MAX TP";
        case Metric::loudnessRange:   return "LRA";
        case Metric::plr:             return "PLR";
    }
    return "";
}

constexpr bool hasUniqueMetrics (Layout layout) noexcept
{
    for (std::size_t index = 0; index < layout.main.size(); ++index)
    {
        for (std::size_t other = index + 1; other < layout.main.size(); ++other)
            if (layout.main[index] == layout.main[other]) return false;
        for (const auto support : layout.support)
            if (layout.main[index] == support) return false;
    }
    for (std::size_t index = 0; index < layout.support.size(); ++index)
        for (std::size_t other = index + 1; other < layout.support.size(); ++other)
            if (layout.support[index] == layout.support[other]) return false;
    return true;
}

static_assert (hasUniqueMetrics (layoutFor (true)));
static_assert (hasUniqueMetrics (layoutFor (false)));
}
