#pragma once

#include "LiveCompareRing.h"

#include <cstdint>
#include <string>

namespace hypha::live_compare
{
// The pair key both roles derive from the PRE instance identity without talking to each other.
std::uint64_t pairKeyForPreInstance (const std::string& preInstanceId) noexcept;

// Stage 1 maps the ring with POSIX shared memory; Windows has no live compare until its stage.
constexpr bool sharedRingAvailable() noexcept
{
#if defined (_WIN32)
    return false;
#else
    return true;
#endif
}

// "/kh-lc-" and 16 hex digits: 23 bytes, within the 31-byte POSIX shared-memory name limit.
std::string sharedRingName (std::uint64_t pairKey);

// Non-RT owner of one mapping. PRE creates the ring for its own identity when its writes are
// enabled; POST opens that ring when the user starts a live session and raises its demand flag.
// The Audio Thread sees the Ring only through a publication slot and never maps, unmaps or names
// anything. Until the Windows stage, create and open return false on Windows (live compare
// unavailable there).
class SharedRingMapping
{
public:
    SharedRingMapping() = default;
    ~SharedRingMapping();
    SharedRingMapping (const SharedRingMapping&) = delete;
    SharedRingMapping& operator= (const SharedRingMapping&) = delete;

    bool create (std::uint64_t pairKey, std::uint32_t sampleRate); // PRE
    bool open (std::uint64_t pairKey, std::uint32_t sampleRate);   // POST
    void close() noexcept;

    Ring* ring() const noexcept { return mapped; }
    std::uint64_t key() const noexcept { return pairKeyValue; }
    std::uint32_t rate() const noexcept { return sampleRateValue; }

private:
    Ring* mapped = nullptr;
    bool owner = false;
    std::uint64_t pairKeyValue = 0;
    std::uint32_t sampleRateValue = 0;
    std::string name;
};
}
