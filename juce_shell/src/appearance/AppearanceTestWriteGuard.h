#pragma once
#include "../StorageWriteGuard.h"
// Stage-one fixtures now exercise the shared C++ guard; no added permission authority.
namespace hypha::appearance::test_write_guard {
using hypha::storage::write_guard::FixturePolicy;
using hypha::storage::write_guard::ScopedFixturePolicy;
using hypha::storage::write_guard::permitMutation;
using hypha::storage::write_guard::permittedMutationEntries;
}
