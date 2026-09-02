#pragma once

#include <vector>

#include "kirin_hypha_ffi.h"

namespace hypha::observatory
{
class View;
}

namespace hypha::tests
{
void verifyObservatoryCaptureContract (
    observatory::View& post,
    observatory::View& pre,
    const std::vector<KirinMeterHistoryEntry>& history,
    const KirinObservatoryFrame& activeFrame,
    const KirinObservatoryFrame& inactiveFrame);
}
