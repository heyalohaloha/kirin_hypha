#pragma once

#include "LiveCompareRing.h"

#include <cstdint>
#include <string>

namespace hypha::live_compare
{
// The pair key both roles derive from the PRE instance identity without talking to each other.
std::uint64_t pairKeyForPreInstance (const std::string& preInstanceId) noexcept;

// macOS maps the ring with POSIX shared memory, Windows with a pagefile-backed named section.
constexpr bool sharedRingAvailable() noexcept { return true; }

// "/kh-lc-" and 16 hex digits: 23 bytes, within the 31-byte POSIX shared-memory name limit.
// Windows derives its section names from it ("Local\kh-lc-...-slot").
std::string sharedRingName (std::uint64_t pairKey);

// Non-RT owner of one mapping. PRE creates the ring for its own identity when its writes are
// enabled; POST opens that ring when the user starts a live session and raises its demand flag.
// The Audio Thread sees the Ring only through a publication slot and never maps, unmaps or names
// anything. A ring its PRE closed is never opened again: POSIX removes the name, and on Windows,
// where a name lives while any process holds the section, PRE moves to the next of a few slots.
class SharedRingMapping
{
public:
    SharedRingMapping() = default;
    ~SharedRingMapping();
    SharedRingMapping (const SharedRingMapping&) = delete;
    SharedRingMapping& operator= (const SharedRingMapping&) = delete;

    bool create (std::uint64_t pairKey, std::uint32_t sampleRate, std::uint32_t source = 0); // PRE
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
#if defined (_WIN32)
    void* section = nullptr; // HANDLE of the mapped section
#else
    std::string name;
#endif
};
}
