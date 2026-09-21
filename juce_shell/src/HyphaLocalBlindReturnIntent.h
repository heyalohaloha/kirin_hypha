#pragma once
#include "local_blind/LocalBlindProductSession.h"

namespace hypha::local_blind_ui
{
// One Editor's explicit gesture. Destruction/reopen and project state never transfer it.
class ReturnIntent final
{
public:
    void arm (local_blind::TrialReturnFacts requested) noexcept
    { if (requested.requested()) intent = requested; }
    void clear() noexcept { intent = {}; }
    bool shouldClose (const local_blind::ProductSessionView& state) const noexcept
    {
        return state.phase == local_blind::ProductSessionPhase::returned
            && state.returnFacts.confirmed && intent.sameRequest (state.returnFacts);
    }
private:
    local_blind::TrialReturnFacts intent;
};
}
