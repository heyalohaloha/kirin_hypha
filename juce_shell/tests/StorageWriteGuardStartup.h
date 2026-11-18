#pragma once
#include "../src/StorageWriteGuard.h"
#include <iostream>
namespace hypha::tests {
// Called before any fixture creation, including child-process modes.
inline bool initializeStorageWriteGuard() {
    if (storage::write_guard::enableFromEnvironment()) return true;
    std::cerr << "STORAGE_GUARD_STARTUP_DENIED\n";
    return false;
}
}
