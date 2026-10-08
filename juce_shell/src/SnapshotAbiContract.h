#pragma once

#include "kirin_hypha_snapshot_abi.h"
#include <cstddef>
#include <cstdint>
#include "kirin_hypha_attack_summary_v2_ffi.h"
#include "kirin_hypha_attack_snapshot_ffi.h"
#include "kirin_hypha_time_snapshot_ffi.h"

namespace kirin
{
inline KirinSnapshotAbiContract expectedSnapshotAbiContract() noexcept
{
    return {
        1u, sizeof (KirinSnapshotAbiContract), 7u, 8u, 64u, 1200u,
        2u, 2u, 2u, 0u,
        sizeof (KirinAttackBandSummaryV2), alignof (KirinAttackBandSummaryV2),
        sizeof (KirinAttackSingleSnapshotV2), alignof (KirinAttackSingleSnapshotV2),
        sizeof (KirinTimeSnapshotV2), alignof (KirinTimeSnapshotV2),
        { offsetof (KirinSnapshotSourceKey, generation), offsetof (KirinSnapshotSourceKey, sample_rate),
          offsetof (KirinSnapshotSourceKey, channels), offsetof (KirinSnapshotSourceKey, odf_hash),
          offsetof (KirinSnapshotEventKey, event_sample), offsetof (KirinSnapshotEventKey, token),
          offsetof (KirinSnapshotInterval, upper), offsetof (KirinSnapshotInterval, unit),
          offsetof (KirinSnapshotScalarEvidence, measurement_revision), offsetof (KirinSnapshotScalarEvidence, class_code),
          offsetof (KirinSnapshotHeader, source), offsetof (KirinSnapshotHeader, band_semantic_hash) },
        { offsetof (KirinAttackBandSummaryV2, cohort_count), offsetof (KirinAttackBandSummaryV2, events),
          offsetof (KirinAttackBandSummaryV2, pair_kind), offsetof (KirinAttackBandSummaryV2, evidence),
          offsetof (KirinAttackBandSummaryV2, lanes), offsetof (KirinAttackBandSummaryV2, head),
          offsetof (KirinAttackBandSummaryV2, tail), offsetof (KirinAttackBandSummaryV2Request, target) },
        { offsetof (KirinAttackSingleSnapshotV2, event), offsetof (KirinAttackSingleSnapshotV2, request_token),
          offsetof (KirinAttackSingleSnapshotV2, measurement_revision), offsetof (KirinAttackSingleSnapshotV2, lanes),
          offsetof (KirinAttackSingleSnapshotV2, pre), offsetof (KirinAttackSingleSnapshotV2, post),
          offsetof (KirinAttackSingleSnapshotV2, pre_valid), offsetof (KirinAttackSingleSnapshotV2, post_valid),
          offsetof (KirinAttackSingleSnapshotV2, all_pre), offsetof (KirinAttackSingleSnapshotV2, all_post) },
        { offsetof (KirinTimeCurrentV2, values), offsetof (KirinTimeComponentV2, pre_span),
          offsetof (KirinTimeComponentV2, binding_revision), offsetof (KirinTimeSnapshotV2, main),
          offsetof (KirinTimeSnapshotV2, psr), offsetof (KirinTimeHistoryEntryV2, ranges),
          offsetof (KirinTimeHistoryEntryV2, valid_count), offsetof (KirinTimeSnapshotRequestV2, main_target) },
        1u, 0u,
    };
}

template <std::size_t N>
inline bool sameOffsets (const std::uint32_t (&a)[N], const std::uint32_t (&b)[N]) noexcept
{
    for (std::size_t i = 0; i < N; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

inline bool snapshotAbiMatches (const KirinSnapshotAbiContract& lib) noexcept
{
    const auto expected = expectedSnapshotAbiContract();
    return lib.version == expected.version
        && lib.struct_size == expected.struct_size
        && lib.legacy_revision == expected.legacy_revision
        && lib.drum_cohort_capacity == expected.drum_cohort_capacity
        && lib.time_raw_capacity == expected.time_raw_capacity
        && lib.time_history_capacity == expected.time_history_capacity
        && lib.summary_version == expected.summary_version
        && lib.single_version == expected.single_version
        && lib.time_version == expected.time_version
        && lib.reserved == expected.reserved
        && lib.summary_size == expected.summary_size
        && lib.summary_align == expected.summary_align
        && lib.single_size == expected.single_size
        && lib.single_align == expected.single_align
        && lib.time_size == expected.time_size
        && lib.time_align == expected.time_align
        && sameOffsets (lib.common_offsets, expected.common_offsets)
        && sameOffsets (lib.summary_offsets, expected.summary_offsets)
        && sameOffsets (lib.single_offsets, expected.single_offsets)
        && sameOffsets (lib.time_offsets, expected.time_offsets)
        && lib.enum_revision == expected.enum_revision
        && lib.layout_reserved == 0;
}

inline bool snapshotAbiMatchesLinkedLibrary() noexcept
{
    KirinSnapshotAbiContract lib {};
    return kirin_hypha_snapshot_abi_contract (1u, sizeof (lib), &lib) == KIRIN_SNAPSHOT_SUCCESS
        && snapshotAbiMatches (lib);
}
} // namespace kirin
