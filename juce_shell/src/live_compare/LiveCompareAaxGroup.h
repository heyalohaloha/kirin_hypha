#pragma once

#include <cstdint>

namespace hypha::live_compare
{
// INV-LC9. Pro Tools names an instance group for each AAX instance (JUCE patch 0010, AAX SDK 2.9
// GetInstanceGroupID), and the instances of one multi-mono set share it. It processes the channels
// of a set on parallel threads (G1 record, section 10), so no instance can switch every channel in
// the same block. A mono AAX instance is therefore offered the live compare only when the host
// names its group and it is the group's only instance, as on a mono track. Each binary counts its
// own instances; the host names a group once, before the first prepare, on a non-audio thread.
class AaxGroupMembership
{
public:
    AaxGroupMembership() = default;
    AaxGroupMembership (const AaxGroupMembership&) = delete;
    AaxGroupMembership& operator= (const AaxGroupMembership&) = delete;
    ~AaxGroupMembership() { assign (0, false); }

    void assign (std::uint64_t group, bool known);
    // The host named the group and no other live instance of this binary shares it.
    bool alone() const noexcept;

private:
    std::uint64_t id = 0;
    bool isKnown = false;
};
}
