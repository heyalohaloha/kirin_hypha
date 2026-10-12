#pragma once
#include <cstdint>

namespace hypha::host_availability
{
inline bool outputLatencyUnavailable (std::uint8_t status) noexcept
{
#ifdef KIRIN_SPECTRUM_OUTPUT_LATENCY_UNAVAILABLE
    static_assert (KIRIN_SPECTRUM_OUTPUT_LATENCY_UNAVAILABLE == 6u, "Output latency integration ABI changed");
    return status == KIRIN_SPECTRUM_OUTPUT_LATENCY_UNAVAILABLE;
#else
    return status == 6u; // integration ABI; older headers do not name it yet
#endif
}
// Shared shell projection of the additive integration ABI; no saved-state/schema change.
#ifdef KIRIN_COMPARISON_REASON_LOCAL_BYPASSED
inline constexpr std::uint8_t postBypassedReason = KIRIN_COMPARISON_REASON_LOCAL_BYPASSED;
static_assert (postBypassedReason == 12u, "POST bypass integration ABI changed");
#else
inline constexpr std::uint8_t postBypassedReason = 12u;
#endif
inline const char* outputLatencyText() noexcept
{
    return "This DAW does not report output latency; this view is unavailable";
}
inline const char* timingText() noexcept
{
    return "This DAW cannot verify timing; comparison is unavailable";
}
}
