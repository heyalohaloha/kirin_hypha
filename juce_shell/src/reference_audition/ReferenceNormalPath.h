#pragma once
#include <cstdint>

namespace hypha::reference_audition
{
class AudioPages;

// What one normal audition block plays: the PCM bank, where the host position lands in it and the
// gain matched to that source. The worker replaces the whole set inside one mapping publication,
// so the Audio Thread never joins new PCM to the previous song's gain or position.
struct NormalPath
{
    AudioPages* pages = nullptr;
    std::uint64_t pageGeneration = 0, mapping = 0, switchSerial = 0;
    std::int64_t start = 0, end = 0, hostAnchor = 0, sourceAnchor = 0;
    float gain = 1.0f;
    bool loops = false, locked = false, valid = false;

    std::int64_t position (std::int64_t hostPosition, bool wholeVersion) const noexcept;
    std::int64_t looped (std::int64_t hostPosition) const noexcept;
    bool restarts (bool wholeVersion) const noexcept { return ! wholeVersion && ! locked && ! loops && end > start; }
};
}
