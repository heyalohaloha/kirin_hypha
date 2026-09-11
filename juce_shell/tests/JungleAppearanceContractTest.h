#pragma once

#include <vector>

#include "../src/HyphaObservatoryView.h"

namespace hypha::tests
{
void verifyJungleAppearanceContract (
    const KirinMeterSession&,
    const KirinWatchDisplay&,
    const std::vector<KirinMeterHistoryEntry>&,
    const KirinObservatoryFrame&);
}
