#pragma once

namespace hypha::live_compare
{
// A test re-prepares the host format here: after LISTEN read its start conditions, before the ring opens.
inline void (*beforeRendererLeaseForTest)() = nullptr;

// Message thread only. The renderer shares the ring's RT lease: no buffer may be resized
// while a callback can still enter through the old publication.
template <typename Slot, typename Collect, typename Prepare>
bool prepareRendererAfterLease (Slot& slot, bool fadePending, Collect&& collect, Prepare&& prepare)
{
    if (beforeRendererLeaseForTest) beforeRendererLeaseForTest();
    if (fadePending) return false;
    slot.retire();
    if (! collect (slot)) return false;
    prepare();
    return true;
}
}
