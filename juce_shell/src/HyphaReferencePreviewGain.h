#pragma once
#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
// Lower-A approval retains the required MATCH from the offered A window.
// Its amount is the negative of that gain (referenceAttenuationToMatch).
// Keep the selected preview on the same observation until the offer ends.
inline double songPreviewGainDb (const State& state, const SongFact& fact, bool selected) noexcept
{
    const auto& offer = state.action.offer;
    if (selected && state.action.kind == ActionKind::lowerAAndPlay && offer.slot == 3
        && std::isfinite (offer.db) && offer.db < 0.0)
        return -offer.db;
    // FOLLOW A は曲全体の値で合わせる。曲全体の値は選んでいる曲のものだけが届くので、ほかの曲は押すまで出さない。
    if (state.followA)
        return selected ? state.aWindowLoudness - (state.wholeSongLoudness + fact.loudnessOffsetLu) : std::numeric_limits<double>::quiet_NaN();
    return songMatchGainDb (state, fact);
}
}
