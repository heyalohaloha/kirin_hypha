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

// What the value is and what it is used for (2026-10-04 Daisuke「PLRはダイナミクスの平均とか分かるように…活用方法も含めて」).
// Facts and uses only, no judgement of the value (R-22). Shown after "POST. ", "PRE. " or "POST minus PRE. ".
constexpr const char* scopeHelp (Metric metric) noexcept
{
    switch (metric)
    {
        case Metric::momentary:
            return "Momentary loudness (M): the loudness over 400 ms, moment by moment. Read it while playing to compare passages.";
        case Metric::shortTerm:
            return "Short-term loudness (S): the loudness over 3 seconds, phrase by phrase. Compare sections such as verse and chorus.";
        case Metric::integrated:
            return "Integrated loudness (I): the whole song since the last Meter Session reset. Compare it with a delivery target such as -14 LUFS.";
        case Metric::maximumTruePeak:
            return "Highest true peak (MAX TP) since the last Meter Session reset, inter-sample peaks included. Compare it with a ceiling such as -1 dBTP.";
        case Metric::loudnessRange:
            return "Loudness range (LRA): how far the loudness moves within the song since the reset. Compare PRE and POST to see how much compression narrowed it.";
        case Metric::plr:
            return "Average dynamics of the song (PLR): the highest true peak minus integrated loudness. Compare PRE and POST to see how much limiting reduced it.";
        case Metric::truePeak:
            return "True peak (TP) in the current window, inter-sample peaks included. Watch it against a ceiling such as -1 dBTP.";
        case Metric::crest:
            return "Crest factor (CREST): peak minus RMS in this window, the dynamics of the moment. Compare PRE and POST to see how much transients were reduced.";
        case Metric::psr:
            return "Peak to short-term loudness (PSR) in the current window: the dynamics heard now. Compare PRE and POST to see how much they were reduced.";
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
