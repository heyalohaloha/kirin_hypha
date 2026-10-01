#pragma once
#include <cstdint>
#include <limits>

namespace hypha
{
// An additional raw clock, deliberately separate from the content timeline. A clock becomes
// correspondence evidence only together with the explicit authority/bound recorded by the
// live-compare policy; matching numbers alone never prove a common loop occurrence.
enum class AuxiliaryClockSource : std::uint8_t
{
    unavailable = 0, vst3Continuous = 1, audioUnitRender = 2, aaxEngine = 3
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
        case AuxiliaryClockSource::aaxEngine: return "AAX DAE clock";
        case AuxiliaryClockSource::unavailable: return "unavailable";
    }
    return "unavailable";
}
}
