#pragma once

namespace hypha::automatic_pair
{
template <typename Allowed, typename Unpair, typename IsUnpaired, typename Notify>
void unpairNotice (Allowed allowed, Unpair unpair, IsUnpaired isUnpaired, Notify notify)
{
    if (! allowed()) return;
    unpair();
    if (isUnpaired()) notify();
}
}
