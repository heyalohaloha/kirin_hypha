#pragma once
#include "../src/StorageWriteGuard.h"
namespace hypha::tests {
template<class Require> juce::File shippingDisabledFixture(Require require) {
    const auto allowed=juce::SystemStats::getEnvironmentVariable("KIRIN_HYPHA_TEST_STORAGE_ROOT",{});
    const auto destination=juce::SystemStats::getEnvironmentVariable("KIRIN_HYPHA_TEST_DISABLED_DESTINATION",{});
    require(storage::write_guard::validRoot(allowed),"disabled mode has valid allowed root A");
    require(destination.isNotEmpty() && juce::File::isAbsolutePath(destination),"explicit disposable destination B");
    const juce::File a(allowed), b(destination);
    require(b.isDirectory() && b!=a && !b.isAChildOf(a) && !a.isAChildOf(b),"A and B are disjoint existing fixtures");
    return b;
}
}
