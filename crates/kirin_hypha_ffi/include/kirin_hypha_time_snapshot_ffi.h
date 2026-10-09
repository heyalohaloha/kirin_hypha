#ifndef KIRIN_HYPHA_TIME_SNAPSHOT_FFI_H
#define KIRIN_HYPHA_TIME_SNAPSHOT_FFI_H
#include <stdbool.h>
#include <stdint.h>
#include "kirin_hypha_channels.h"
#include "kirin_hypha_snapshot_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define KIRIN_TIME_SNAPSHOT_VERSION 2u
#define KIRIN_TIME_HISTORY_CAPACITY 1200u
#define KIRIN_TIME_RAW_CAPACITY 64u
enum { KIRIN_TIME_PRE = KIRIN_TARGET_PRE, KIRIN_TIME_POST = KIRIN_TARGET_POST, KIRIN_TIME_DELTA = KIRIN_TARGET_DELTA };
enum { KIRIN_TIME_CURRENT_LIVE = 1, KIRIN_TIME_CURRENT_MISSING = 2,
       KIRIN_TIME_CURRENT_EXPIRED = 3, KIRIN_TIME_CURRENT_STOPPED = 4,
       KIRIN_TIME_CURRENT_WAITING = 5 };
typedef struct {
    uint64_t epoch, incarnation, generation, token;
    uint32_t sample_rate; uint8_t channels, reserved[3];
} KirinTimeSourceSpanV2;
typedef struct {
    KirinTimeSourceSpanV2 span;
    uint64_t cutoff, run; int64_t endpoint;
    double completion_age_ms, remaining_ms;
    /* M,S,TP,PSR,PLR,CORR. NaN is missing. No last-finite reconstruction. */
    double values[6];
    uint8_t target, state, clock, finite_mask, reserved[4];
} KirinTimeCurrentV2;
enum { KIRIN_TIME_REASON_NONE = 0, KIRIN_TIME_REASON_WAITING = 1,
       KIRIN_TIME_REASON_ACTIVE = 2, KIRIN_TIME_REASON_STOPPED = 3,
       KIRIN_TIME_REASON_INCOMPATIBLE = 4, KIRIN_TIME_REASON_MISSING = 5 };
typedef struct {
    KirinTimeCurrentV2 current;
    KirinTimeSourceSpanV2 pre_span;
    uint64_t pre_run, binding_revision, locator_identity, owner_identity, claim_identity;
    uint32_t history_count; uint8_t history_hold, reason, reserved[2];
} KirinTimeComponentV2;
typedef struct {
    uint32_t version, struct_size; uint64_t revision, local_cutoff, range_start;
    KirinTimeSourceSpanV2 post_span;
    uint64_t binding_revision;
    uint8_t signal_state, selection_intent, reserved[6];
    KirinTimeComponentV2 main, psr;
} KirinTimeSnapshotV2;
typedef struct {
    uint32_t version, struct_size, packet_size, entry_size;
    uint64_t duration_frames; uint32_t main_target, resolution;
} KirinTimeSnapshotRequestV2;
typedef struct { double min, max, mean; } KirinTimeRangeV2;
typedef struct {
    uint64_t epoch, generation, run, segment, first_observed, last_observed;
    int64_t first_endpoint, last_endpoint;
    /* M,S,TP,CORR,PSR. Means divide by valid_count, never total_count. */
    KirinTimeRangeV2 ranges[5]; uint32_t clip_events[6];
    uint16_t valid_count[5], total_count;
    uint8_t clock, connects_previous, resolution, reserved;
} KirinTimeHistoryEntryV2;
/* UI-only 10Hz acquisition. Arrays are independently bounded to 1200; capacity 0
 * omits history work. PSR capacity 0 omits PSR current and comparison acquisition
 * entirely (compact PSR); main capacity 0 still acquires its current. Unknown version, short size,
 * null and authority/source races leave packet and both arrays completely untouched.
 * Per-bucket observation and finite counts are exact uint16_t values: 65535 is
 * representable; a requested reduction above 65535 returns Unsupported without
 * touching any output. Increase history capacity or shorten the requested range.
 * Capacity 0 still permits current-only acquisition without reducing history.
 * Every output must be disjoint writable storage; this function never does file IO.
 * TTL remaining is strict <400ms from original POST/local completion and must keep
 * decreasing on the GUI monotonic clock even when later polls fail. Anchor the
 * returned remaining_ms to the caller's poll-start clock, never to poll-return
 * time (which would extend the deadline by acquisition overhead). */
uint8_t kirin_hypha_poll_time_snapshot_v2(const KirinHypha* handle,
    const KirinTimeSnapshotRequestV2* request,
    KirinTimeHistoryEntryV2* main_history, uint32_t main_capacity,
    KirinTimeHistoryEntryV2* psr_history, uint32_t psr_capacity,
    KirinTimeSnapshotV2* out);
#ifdef __cplusplus
}
#endif
#endif
