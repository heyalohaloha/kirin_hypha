#pragma once
#include "../../src/live_compare/LiveCompareSharedRing.h"

namespace hypha::tests
{
template <typename Require>
void verifySharedRingAbi (live_compare::SharedRingMapping& pre,
                          live_compare::SharedRingMapping& reader,
                          std::uint64_t pairKey, Require require)
{
    using namespace live_compare;
    for (const auto version : { ringVersion - 1, ringVersion + 1 })
    {
        pre.ring()->header.version.store (version);
        require (! reader.open (pairKey, 48000), "a foreign ring ABI is refused");
    }
    pre.ring()->header.version.store (ringVersion);
}
}
