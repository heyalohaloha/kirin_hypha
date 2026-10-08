// Native C-header consumer of the actual Rust archive; no GUI or real host is involved.
#include "../src/SnapshotAbiContract.h"
#include "kirin_hypha_ffi.h"
#include "kirin_hypha_navigation_snapshot_ffi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

bool runTimeSnapshotProbe();

namespace
{
bool expect (bool result, const char* label)
{
    if (! result)
        std::fprintf (stderr, "snapshot ABI contract failed: %s\n", label);
    return result;
}

bool attackVersionStatuses()
{
    const std::array<std::uint8_t, 1> roles { KIRIN_CHANNEL_ROLE_CENTRE };
    std::unique_ptr<KirinHypha, decltype (&kirin_hypha_destroy)> engine (
        kirin_hypha_create (48000, roles.data(), 1), kirin_hypha_destroy);
    if (! expect (engine != nullptr, "version probe engine")) return false;
    struct alignas (8) Prefix { std::uint32_t version, size; } prefix { 3, 128 };
    struct GuardedSummary { KirinAttackBandSummaryV2 value; std::array<std::uint8_t, 16> tail; } summary;
    struct GuardedToken { std::uint64_t value; std::array<std::uint8_t, 16> tail; } token;
    std::memset (&summary, 0xa5, sizeof (summary));
    std::memset (&token, 0xa5, sizeof (token));
    std::array<std::uint8_t, sizeof (summary)> summaryBefore {};
    std::array<std::uint8_t, sizeof (token)> tokenBefore {};
    std::memcpy (summaryBefore.data(), &summary, sizeof (summary));
    std::memcpy (tokenBefore.data(), &token, sizeof (token));
    const auto unchanged = [&] {
        return std::memcmp (&summary, summaryBefore.data(), sizeof (summary)) == 0
            && std::memcmp (&token, tokenBefore.data(), sizeof (token)) == 0;
    };
    // An unknown ABI has only the stable prefix in common. Its size cannot imply V2.
    for (const auto size : { 4u, 8u, 15u, 16u, 32u, 95u, 96u, 128u })
    {
        if (! expect (kirin_hypha_poll_attack_band_summary_v2 (engine.get(), size,
                reinterpret_cast<const KirinAttackBandSummaryV2Request*> (&prefix),
                sizeof (summary.value), &summary.value) == KIRIN_SNAPSHOT_UNSUPPORTED,
                "summary future version before size")
            || ! expect (kirin_hypha_request_attack_single_v2 (engine.get(), size,
                reinterpret_cast<const KirinAttackSingleV2Request*> (&prefix),
                &token.value) == KIRIN_SNAPSHOT_UNSUPPORTED, "single future version before size")
            || ! expect (unchanged(), "future version output canaries")) return false;
    }
    if (! expect (kirin_hypha_poll_attack_band_summary_v2 (engine.get(), 32,
            reinterpret_cast<const KirinAttackBandSummaryV2Request*> (&prefix), 0,
            &summary.value) == KIRIN_SNAPSHOT_UNSUPPORTED, "future version before output size")) return false;
    for (const auto size : { 0u, 3u })
    {
        if (! expect (kirin_hypha_poll_attack_band_summary_v2 (engine.get(), size,
                reinterpret_cast<const KirinAttackBandSummaryV2Request*> (&prefix),
                sizeof (summary.value), &summary.value) == KIRIN_SNAPSHOT_INVALID_REQUEST,
                "summary short version prefix")
            || ! expect (kirin_hypha_request_attack_single_v2 (engine.get(), size,
                reinterpret_cast<const KirinAttackSingleV2Request*> (&prefix),
                &token.value) == KIRIN_SNAPSHOT_INVALID_REQUEST, "single short version prefix")
            || ! expect (unchanged(), "short prefix output canaries")) return false;
    }
    alignas (8) std::array<std::uint32_t, 32> prefixStorage {};
    prefixStorage[1] = 3;
    prefixStorage[2] = 128;
    const auto* u32Prefix = prefixStorage.data() + 1;
    if (! expect (reinterpret_cast<std::uintptr_t> (u32Prefix) % 8 == 4,
                  "four-byte-only aligned version prefix")) return false;
    for (const auto size : { 4u, 8u, 95u, 96u, 128u })
        if (! expect (kirin_hypha_request_attack_single_v2 (engine.get(), size,
                reinterpret_cast<const KirinAttackSingleV2Request*> (u32Prefix),
                &token.value) == KIRIN_SNAPSHOT_UNSUPPORTED, "future prefix before V2 alignment")
            || ! expect (unchanged(), "u32 prefix output canaries")) return false;
    prefixStorage[1] = 2;
    prefixStorage[2] = 96;
    if (! expect (kirin_hypha_request_attack_single_v2 (engine.get(), 96,
            reinterpret_cast<const KirinAttackSingleV2Request*> (u32Prefix),
            &token.value) == KIRIN_SNAPSHOT_INVALID_REQUEST, "known V2 full alignment")) return false;
    return expect (unchanged(), "all version status outputs unchanged");
}
bool g2BoundaryCanaries()
{
    struct Guarded { KirinMeterSessionV2 packet; std::array<uint8_t, 16> tail; } session;
    std::memset (&session, 0xa5, sizeof (session));
    std::array<uint8_t, sizeof (session)> before {};
    std::memcpy (before.data(), &session, sizeof (session));
    for (const auto version : { 0u, 3u, UINT32_MAX })
        if (! expect (kirin_hypha_poll_meter_session_v2 (nullptr, version, 0, &session.packet)
                == KIRIN_SNAPSHOT_UNSUPPORTED, "Session future version before buffer validation")
            || ! expect (std::memcmp (&session, before.data(), sizeof (session)) == 0,
                         "Session future output unchanged")) return false;
    if (! expect (kirin_hypha_poll_meter_session_v2 (nullptr, 2, sizeof (session), &session.packet)
            == KIRIN_SNAPSHOT_INVALID_REQUEST, "Session null handle")
        || ! expect (std::memcmp (&session, before.data(), sizeof (session)) == 0,
                     "Session invalid output unchanged")) return false;
    auto navigation = std::make_unique<KirinAttackNavigationV2>();
    std::memset (navigation.get(), 0xa5, sizeof (*navigation));
    std::array<uint8_t, sizeof (*navigation)> old {};
    std::memcpy (old.data(), navigation.get(), old.size());
    const std::array<std::uint8_t, 1> roles { KIRIN_CHANNEL_ROLE_CENTRE };
    std::unique_ptr<KirinHypha, decltype (&kirin_hypha_destroy)> engine (
        kirin_hypha_create (48000, roles.data(), 1), kirin_hypha_destroy);
    if (! expect (engine != nullptr, "navigation prefix probe engine")) return false;
    const KirinAttackNavigationRequestV2 future { 3, 999, 0, {} };
    for (const auto size : { 4u, 8u, 15u, 16u, 99u })
        if (! expect (kirin_hypha_poll_attack_navigation_v2 (engine.get(), size, &future, 0,
                navigation.get()) == KIRIN_SNAPSHOT_UNSUPPORTED,
                "navigation future version before caller sizes")
            || ! expect (std::memcmp (navigation.get(), old.data(), old.size()) == 0,
                         "navigation future output unchanged")) return false;
    return true;
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

    static_assert (sizeof (KirinAttackNavigationRequestV2) == 16);
    static_assert (alignof (KirinAttackNavigationRequestV2) == 4);
    static_assert (sizeof (KirinAttackNavigationV2) == 67600);
    static_assert (alignof (KirinAttackNavigationV2) == 8);
    static_assert (offsetof (KirinAttackNavigationV2, count) == 136);
    static_assert (offsetof (KirinAttackNavigationV2, events) == 144);
    static_assert (offsetof (KirinAttackNavigationV2, pair_kind) == 19344);
    static_assert (offsetof (KirinAttackNavigationV2, post) == 19584);
    static_assert (offsetof (KirinAttackNavigationV2, pre) == 43592);
    static_assert (sizeof (KirinMeterSessionV2) == 1872);
    static_assert (alignof (KirinMeterSessionV2) == 8);
    static_assert (offsetof (KirinMeterSessionV2, session) == 8);
    static_assert (offsetof (KirinMeterSessionV2, processed_frames) == 1848);
    static_assert (offsetof (KirinMeterSessionV2, pending_frames) == 1856);
    static_assert (offsetof (KirinMeterSessionV2, summary_status) == 1864);

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
    if (! attackVersionStatuses() || ! g2BoundaryCanaries() || ! runTimeSnapshotProbe()) return 1;
    std::puts ("snapshot ABI native contract: PASS");
    return 0;
}
