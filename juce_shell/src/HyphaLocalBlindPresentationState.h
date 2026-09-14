#pragma once
#include "local_blind/LocalBlindProductSession.h"
#include <tuple>

namespace hypha::local_blind_ui
{
inline bool samePresentation (const local_blind::ProductSessionView& a,
                              const local_blind::ProductSessionView& b) noexcept
{
    const auto facts = [] (const local_blind::ProductSessionView& v)
    {
        const auto& t = v.trial; const auto& r = v.returnFacts;
        return std::make_tuple (v.phase, v.failure, v.preparationFailure, v.canRecapture,
            v.gainPolicy, v.sampleRate, v.channels, v.start, v.frames, v.fixedPreGainDb,
            v.lowerPostGainDb, v.matchedAnalysisUnits, t.phase, t.activeStimulus,
            t.pendingStimulus, t.revealedOneSide, t.answer, t.canAnswer,
            t.lowerPostApprovalRequired, t.passComplete, t.heardOneComplete,
            t.heardTwoComplete, t.failure, r.scope, r.capture, r.command, r.confirmed,
            r.attenuationApplied);
    };
    return facts (a) == facts (b);
}
}
