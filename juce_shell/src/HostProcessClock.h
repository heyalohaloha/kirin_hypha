#pragma once
#include <cstdint>

namespace hypha
{
// Raw host observations. Optional presentation values are not qualified PDC authority.
struct HostProcessClock
{
    bool playing = false;
    bool hasPosition = false;
    std::uint8_t clockSource = 0;
    std::int64_t positionSamples = 0;
    bool hasClockEnd = false;
    std::int64_t clockStartSamples = 0;
    std::int64_t clockEndSamples = 0;
    std::uint8_t presentationSource = 0;
    bool inputPresentationValid = false;
    std::uint32_t inputPresentationSamples = 0;
    bool outputPresentationValid = false;
    std::uint32_t outputPresentationSamples = 0;
};
}
