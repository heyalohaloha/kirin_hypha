#pragma once
#include "../../src/live_compare/LiveCompareSharedRing.h"
#if ! defined (_WIN32)
 #include <fcntl.h>
 #include <sys/mman.h>
#endif

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
   #if ! defined (_WIN32)
    // A POST of the previous version looks for its own versioned name and finds no PRE at all.
    auto olderName = sharedRingName (pairKey);
    olderName.replace (0, 8, "/kh-lc10");
    require (shm_open (olderName.c_str(), O_RDWR, 0) < 0, "a reader of the previous version cannot find this ring");
   #endif
}
}
