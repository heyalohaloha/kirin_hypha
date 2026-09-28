#include "LiveCompareAaxGroup.h"

#include <map>
#include <mutex>

namespace hypha::live_compare
{
namespace
{
std::mutex& registryLock() noexcept
{
    static std::mutex lock;
    return lock;
}

std::map<std::uint64_t, int>& registry() noexcept
{
    static std::map<std::uint64_t, int> members;
    return members;
}
}

void AaxGroupMembership::assign (std::uint64_t group, bool known)
{
    const std::lock_guard<std::mutex> guard (registryLock());
    auto& members = registry();
    if (isKnown)
        if (const auto it = members.find (id); it != members.end() && --it->second <= 0)
            members.erase (it);
    id = known ? group : 0;
    isKnown = known;
    if (isKnown)
        ++members[id];
}

bool AaxGroupMembership::alone() const noexcept
{
    if (! isKnown)
        return false;
    const std::lock_guard<std::mutex> guard (registryLock());
    const auto it = registry().find (id);
    return it != registry().end() && it->second == 1;
}
}
