#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The note and the highest peaks of a low-frequency curve, for the tags and the readout. Both are
// display-only readings of a curve already measured; neither touches the analysis.
namespace hypha::spectrum_peaks
{
struct Peak
{
    float hz = 0.0f;
    float level = 0.0f;
};

struct Peaks
{
    std::array<Peak, 3> found {};
    std::size_t count = 0;
};

// A peak stands 3 dB above the valley that separates it from anything higher: enough to be a
// note rather than ripple, while two notes a few Hz apart can still both count.
inline constexpr float minimumProminenceDb = 3.0f;
// About a semitone, so two tags never name one note.
inline constexpr float minimumSeparationRatio = 1.06f;
inline constexpr float quietestLevelDbfs = -90.0f;
// The analysis window's own ripple starts about 31 dB under a tone. A tag further below the
// highest peak would be reading the window, not the music.
inline constexpr float tagRangeBelowHighestDb = 30.0f;

inline std::string noteName (float hz)
{
    if (! std::isfinite (hz) || hz <= 0.0f)
        return {};
    const auto midi = static_cast<int> (std::lround (69.0 + 12.0 * std::log2 (hz / 440.0)));
    static constexpr std::array<const char*, 12> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int octave = static_cast<int> (std::floor (midi / 12.0)) - 1;
    const int pitch = midi - (octave + 1) * 12;
    return names[static_cast<std::size_t> (pitch)] + std::to_string (octave);
}

// `hz` rises. A band with nothing to measure is NaN: no peak stands on it and it ends a valley.
inline Peaks find (const float* hz, const float* level, std::size_t count)
{
    Peaks result;
    if (count < 3u)
        return result;
    const auto known = [level] (std::size_t index) { return std::isfinite (level[index]); };
    const auto valleyToward = [&] (std::size_t index, std::ptrdiff_t step)
    {
        float valley = level[index];
        for (auto at = static_cast<std::ptrdiff_t> (index) + step;
             at >= 0 && at < static_cast<std::ptrdiff_t> (count); at += step)
        {
            const auto position = static_cast<std::size_t> (at);
            if (! known (position) || level[position] > level[index])
                return valley;
            valley = std::min (valley, level[position]);
        }
        return valley;
    };
    std::vector<Peak> candidates;
    for (std::size_t next = 1u; next + 1u < count;)
    {
        // A flat top is one peak: its centre and its own height, with nothing to interpolate.
        const auto index = next;
        auto last = index;
        while (last + 1u < count && level[last + 1u] == level[index])
            ++last;
        next = last + 1u;
        if (last + 1u >= count || ! known (index) || ! known (index - 1u) || ! known (last + 1u)
            || level[index - 1u] >= level[index] || level[last + 1u] >= level[index]
            || level[index] < quietestLevelDbfs)
            continue;
        const float base = std::max (valleyToward (index, -1), valleyToward (last, 1));
        if (level[index] - base < minimumProminenceDb)
            continue;
        if (last != index)
        {
            candidates.push_back ({ std::sqrt (hz[index] * hz[last]), level[index] });
            continue;
        }
        const float before = level[index - 1u];
        const float at = level[index];
        const float after = level[index + 1u];
        const float curvature = before - 2.0f * at + after;
        const float offset = curvature < 0.0f ? 0.5f * (before - after) / curvature : 0.0f;
        const float neighbour = hz[offset >= 0.0f ? index + 1u : index - 1u];
        candidates.push_back ({ hz[index] * std::pow (neighbour / hz[index], std::abs (offset)),
                                at - 0.25f * (before - after) * offset });
    }
    std::sort (candidates.begin(), candidates.end(),
               [] (const Peak& left, const Peak& right) { return left.level > right.level; });
    for (const auto& candidate : candidates)
    {
        const auto tooClose = [&candidate] (const Peak& kept)
        {
            return std::max (candidate.hz, kept.hz) / std::min (candidate.hz, kept.hz)
                 < minimumSeparationRatio;
        };
        if (result.count == result.found.size()
            || candidate.level < candidates.front().level - tagRangeBelowHighestDb)
            break;
        const auto kept = result.found.begin() + static_cast<std::ptrdiff_t> (result.count);
        if (std::none_of (result.found.begin(), kept, tooClose))
            result.found[result.count++] = candidate;
    }
    std::sort (result.found.begin(),
               result.found.begin() + static_cast<std::ptrdiff_t> (result.count),
               [] (const Peak& left, const Peak& right) { return left.hz < right.hz; });
    return result;
}
}
