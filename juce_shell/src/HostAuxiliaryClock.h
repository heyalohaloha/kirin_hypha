#pragma once
#include <cstdint>
#include <limits>

namespace hypha
{
// An additional raw clock, deliberately separate from the content timeline. The three
// format clocks have different semantics; none alone proves a common loop occurrence/PDC.
enum class AuxiliaryClockSource : std::uint8_t
{
    unavailable = 0, vst3Continuous = 1, audioUnitRender = 2, aaxNative = 3
};
struct HostAuxiliaryClock
{
    std::int64_t samples = 0;
    AuxiliaryClockSource source = AuxiliaryClockSource::unavailable;
    bool valid = false;
};
inline HostAuxiliaryClock offsetAuxiliaryClock (HostAuxiliaryClock clock,
                                                std::uint64_t frames) noexcept
{
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (! clock.valid || frames > static_cast<std::uint64_t> (maximum)
        || clock.samples > maximum - static_cast<std::int64_t> (frames))
        return {};
    clock.samples += static_cast<std::int64_t> (frames);
    return clock;
}
inline const char* auxiliaryClockName (AuxiliaryClockSource source) noexcept
{
    switch (source)
    {
        case AuxiliaryClockSource::vst3Continuous: return "VST3 continuous";
        case AuxiliaryClockSource::audioUnitRender: return "AU render";
        case AuxiliaryClockSource::aaxNative: return "AAX native";
        case AuxiliaryClockSource::unavailable: return "unavailable";
    }
    return "unavailable";
}
}
