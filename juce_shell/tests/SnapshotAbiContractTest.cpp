// Native C-header consumer of the actual Rust archive; no GUI or real host is involved.
#include "../src/SnapshotAbiContract.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

bool runTimeSnapshotProbe();

namespace
{
bool expect (bool result, const char* label)
{
    if (! result)
        std::fprintf (stderr, "snapshot ABI contract failed: %s\n", label);
    return result;
}
}

int main()
{
    static_assert (KIRIN_TARGET_POST == 0 && KIRIN_TARGET_DELTA == 1 && KIRIN_TARGET_PRE == 2);
    static_assert (KIRIN_SNAPSHOT_SUCCESS == 0 && KIRIN_SNAPSHOT_BUSY == 1
        && KIRIN_SNAPSHOT_INVALID_REQUEST == 2 && KIRIN_SNAPSHOT_UNSUPPORTED == 3
        && KIRIN_SNAPSHOT_RETIRED == 4);
    static_assert (sizeof (KirinSnapshotAbiContract) == 248);
    static_assert (alignof (KirinSnapshotAbiContract) == 8);
    static_assert (offsetof (KirinSnapshotAbiContract, summary_size) == 40);
    static_assert (offsetof (KirinSnapshotAbiContract, time_align) == 80);
    static_assert (sizeof (KirinSnapshotSourceKey) == 64);
    static_assert (sizeof (KirinSnapshotEventKey) == 80);
    static_assert (sizeof (KirinSnapshotEndpoint) == 16);
    static_assert (sizeof (KirinSnapshotInterval) == 40);
    static_assert (sizeof (KirinSnapshotScalarEvidence) == 104);
    static_assert (sizeof (KirinSnapshotHeader) == 136);

    static_assert (offsetof (KirinSnapshotSourceKey, generation) == 16);
    static_assert (offsetof (KirinSnapshotSourceKey, odf_hash) == 32);
    static_assert (offsetof (KirinSnapshotEventKey, event_sample) == 64);
    static_assert (offsetof (KirinSnapshotInterval, upper) == 16);
    static_assert (offsetof (KirinSnapshotInterval, unit) == 32);
    static_assert (offsetof (KirinSnapshotScalarEvidence, measurement_revision) == 40);
    static_assert (offsetof (KirinSnapshotScalarEvidence, class_code) == 96);
    static_assert (offsetof (KirinSnapshotHeader, source) == 40);
    static_assert (offsetof (KirinSnapshotHeader, band_semantic_hash) == 104);
    static_assert (sizeof (KirinAttackBandSummaryV2Request) == 16);
    static_assert (sizeof (KirinAttackBandLaneSummaryV2) == 112);
    static_assert (sizeof (KirinAttackBandAveragePointV2) == 56);
    static_assert (sizeof (KirinAttackBandSummaryV2) == 13528);
    static_assert (offsetof (KirinAttackBandLaneSummaryV2, whole_interval) == 8);
    static_assert (offsetof (KirinAttackBandLaneSummaryV2, exact_median) == 48);
    static_assert (offsetof (KirinAttackBandLaneSummaryV2, reason_count) == 72);
    static_assert (offsetof (KirinAttackBandAveragePointV2, post_mean) == 32);
    static_assert (offsetof (KirinAttackBandSummaryV2, events) == 144);
    static_assert (offsetof (KirinAttackBandSummaryV2, evidence) == 792);
    static_assert (offsetof (KirinAttackBandSummaryV2, lanes) == 4120);
    static_assert (offsetof (KirinAttackBandSummaryV2, head) == 4568);
    static_assert (offsetof (KirinAttackBandSummaryV2, tail) == 9944);

    static_assert (sizeof (KirinTimeSourceSpanV2) == 40);
    static_assert (sizeof (KirinTimeCurrentV2) == 136);
    static_assert (sizeof (KirinTimeComponentV2) == 224);
    static_assert (sizeof (KirinTimeSnapshotV2) == 536);
    static_assert (sizeof (KirinTimeHistoryEntryV2) == 224);
    static_assert (sizeof (KirinTimeSnapshotRequestV2) == 32);
    static_assert (offsetof (KirinTimeCurrentV2, values) == 80);
    static_assert (offsetof (KirinTimeComponentV2, pre_span) == 136);
    static_assert (offsetof (KirinTimeComponentV2, binding_revision) == 184);
    static_assert (offsetof (KirinTimeSnapshotV2, main) == 88);
    static_assert (offsetof (KirinTimeSnapshotV2, psr) == 312);
    static_assert (offsetof (KirinTimeHistoryEntryV2, ranges) == 64);
    static_assert (offsetof (KirinTimeHistoryEntryV2, valid_count) == 208);

    static_assert (sizeof (KirinAttackSingleSnapshotV2) == 4560);
    static_assert (sizeof (KirinAttackSingleV2Request) == 96);
    static_assert (sizeof (KirinAttackDetail) == 512);
    static_assert (offsetof (KirinAttackSingleSnapshotV2, event) == 136);
    static_assert (offsetof (KirinAttackSingleSnapshotV2, request_token) == 216);
    static_assert (offsetof (KirinAttackSingleSnapshotV2, lanes) == 240);
    static_assert (offsetof (KirinAttackSingleSnapshotV2, pre) == 656);
    static_assert (offsetof (KirinAttackSingleSnapshotV2, post) == 1936);
    static_assert (offsetof (KirinAttackSingleSnapshotV2, pre_valid) == 3216);
    static_assert (offsetof (KirinAttackSingleSnapshotV2, post_valid) == 3376);
    static_assert (offsetof (KirinAttackSingleSnapshotV2, all_pre) == 3536);
    static_assert (offsetof (KirinAttackSingleSnapshotV2, all_post) == 4048);

    struct Guarded {
        KirinSnapshotAbiContract contract;
        std::array<std::uint8_t, 16> tail;
    } guarded;
    static_assert (sizeof (Guarded) == 264);
    std::memset (&guarded, 0xa5, sizeof (guarded));
    std::array<std::uint8_t, 264> original {};
    std::memcpy (original.data(), &guarded, sizeof (guarded));
    auto* output = &guarded.contract;
    const auto unchanged = [&] {
        return std::memcmp (&guarded, original.data(), sizeof (guarded)) == 0;
    };
    for (const auto version : { 0u, 2u, UINT32_MAX })
        if (! expect (kirin_hypha_snapshot_abi_contract (version, 264, output) == KIRIN_SNAPSHOT_UNSUPPORTED, "unknown version")
            || ! expect (unchanged(), "unknown version canary"))
            return 1;
    for (const auto size : { 0u, 247u })
        if (! expect (kirin_hypha_snapshot_abi_contract (1, size, output) == KIRIN_SNAPSHOT_INVALID_REQUEST, "short buffer")
            || ! expect (unchanged(), "short buffer canary"))
            return 1;
    auto* misaligned = reinterpret_cast<KirinSnapshotAbiContract*> (
        reinterpret_cast<std::uint8_t*> (&guarded) + 1);
    if (! expect (kirin_hypha_snapshot_abi_contract (1, 248, misaligned)
                  == KIRIN_SNAPSHOT_INVALID_REQUEST, "misaligned buffer")
        || ! expect (unchanged(), "misaligned buffer canary")) return 1;
    if (! expect (kirin_hypha_snapshot_abi_contract (1, 248, nullptr) == KIRIN_SNAPSHOT_INVALID_REQUEST, "null")
        || ! expect (kirin_hypha_snapshot_abi_contract (1, 264, output) == KIRIN_SNAPSHOT_SUCCESS, "sized handshake")
        || ! expect (kirin::snapshotAbiMatches (*output), "C and Rust layout"))
        return 1;
    for (const auto byte : guarded.tail)
        if (! expect (byte == 0xa5, "oversized caller tail"))
            return 1;
    if (! runTimeSnapshotProbe()) return 1;
    std::puts ("snapshot ABI native contract: PASS");
    return 0;
}
