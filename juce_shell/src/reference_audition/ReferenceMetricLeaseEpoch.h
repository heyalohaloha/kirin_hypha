#pragma once
#include <cstdint>
#include <limits>

namespace hypha::reference_audition::metrics
{
// All access is under the controller stateLock. An acknowledgement is issued
// only after both worker-owned runtimes have applied the captured lease.
class LeaseEpoch final
{
public:
    void changed() noexcept
    {
        if (requested == std::numeric_limits<std::uint64_t>::max()) exhausted = true;
        else ++requested;
    }
    std::uint64_t ticket() const noexcept { return requested; }
    void acknowledge (std::uint64_t value) noexcept { applied = value; }
    bool current() const noexcept { return !exhausted && requested == applied; }
private:
    std::uint64_t requested = 0, applied = 0;
    bool exhausted = false;
};
}
