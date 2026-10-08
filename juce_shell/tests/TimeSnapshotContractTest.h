#pragma once
#include "kirin_hypha_ffi.h"
#include <vector>
namespace hypha::observatory { class View; }
namespace hypha::tests
{
void verifyTimeSnapshotContract();
void applyTimeSnapshotFixture (observatory::View&, const std::vector<KirinMeterHistoryEntry>&,
                               bool delta = false);
}
