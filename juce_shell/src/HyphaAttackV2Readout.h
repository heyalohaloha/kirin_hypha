#pragma once
#include "HyphaAttackV2Model.h"
#include <algorithm>
#include <iterator>

namespace hypha::attack_v2
{
// A transport discontinuity changes the pass, not the instance or measurement definition.
// This comparison is only for a labelled previous readout, never for admitting measurements.
inline bool sameReadoutOwner (const KirinSnapshotHeader& a, const KirinSnapshotHeader& b)
{
    auto source = a.source;
    source.generation = b.source.generation;
    return sameSource (source, b.source) && a.authority_revision == b.authority_revision
        && std::equal (std::begin (a.band_semantic_hash), std::end (a.band_semantic_hash),
                       std::begin (b.band_semantic_hash));
}
inline const FormattedInterval* mainNumber (const Lane& lane)
{
    if (lane.heldNumber && lane.heldNumber->valid) return &*lane.heldNumber;
    return lane.number.valid && lane.scope != Scope::confirmedSubset && lane.scope != Scope::noScalar
        ? &lane.number : nullptr;
}
inline bool hasMainNumber (const Presentation& p)
{
    return std::any_of (p.lanes.begin(), p.lanes.end(), [] (const auto& lane) { return mainNumber (lane) != nullptr; });
}
inline bool replacesPreviousReadout (const Presentation& p, const PreviousReadout& previous,
                                    const KirinSnapshotHeader& navigation)
{
    return p.summary && p.summary->cohort_count > 0 && hasMainNumber (p)
        && std::equal (p.lanes.begin(), p.lanes.end(), previous.numbers.begin(),
            [] (const auto& lane, const auto& old) { return !old.valid || mainNumber (lane) != nullptr; })
        && (p.header.target == previous.target || p.header.target == KIRIN_TARGET_DELTA)
        && (p.header.source.generation != previous.navigation.source.generation
            || (p.header.target == KIRIN_TARGET_DELTA && previous.target != KIRIN_TARGET_DELTA)
            || (navigation.cutoff_sample < previous.navigation.cutoff_sample
                && p.header.cutoff_sample < previous.navigation.cutoff_sample)
            || p.summary->events[p.summary->cohort_count - 1].event_sample > previous.latestHit);
}
inline std::optional<PreviousReadout> previousReadout (const Presentation& p, const KirinSnapshotHeader& navigation)
{
    if (p.previousReadout) return p.previousReadout;
    if (!hasMainNumber (p) || (!p.summary && !p.single)) return {};
    PreviousReadout previous;
    previous.navigation = navigation.source.generation != 0 ? navigation : p.header;
    previous.band = p.header.band; previous.target = p.header.target;
    previous.count = p.summary ? p.summary->cohort_count : 1;
    previous.latestHit = p.summary && previous.count > 0 ? p.summary->events[previous.count - 1].event_sample
        : p.single ? p.single->event.event_sample : -1;
    for (std::size_t i = 0; i < previous.numbers.size(); ++i)
    {
        previous.numbers[i].unit = p.lanes[i].number.unit;
        if (const auto* number = mainNumber (p.lanes[i])) previous.numbers[i] = *number;
    }
    return previous;
}
}
