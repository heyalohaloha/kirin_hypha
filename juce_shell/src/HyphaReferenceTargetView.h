#pragma once
#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
inline void applyMeasuredTarget (State& state, reference_audition::TargetSnapshot targets, bool licensed)
{
    state.targetsEnabled = licensed;
    state.targets = std::move (targets);
    state.targetSelected = false; // Saved target choices belong to B; C always keeps its Kirin OS Check.
}
}
