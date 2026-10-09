#ifndef KIRIN_HYPHA_NAVIGATION_SNAPSHOT_FFI_H
#define KIRIN_HYPHA_NAVIGATION_SNAPSHOT_FFI_H
#include "kirin_hypha_ffi.h"
#include "kirin_hypha_attack_ffi.h"
#include "kirin_hypha_snapshot_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define KIRIN_ATTACK_NAVIGATION_VERSION 2u
#define KIRIN_ATTACK_NAVIGATION_CAPACITY 240u
typedef struct { uint32_t version, struct_size; uint8_t target, reserved[7]; } KirinAttackNavigationRequestV2;
typedef struct {
    KirinSnapshotHeader header;
    uint32_t count, capacity;
    KirinSnapshotEventKey events[KIRIN_ATTACK_NAVIGATION_CAPACITY];
    uint8_t pair_kind[KIRIN_ATTACK_NAVIGATION_CAPACITY];
    KirinAttackWaveformBatch post, pre;
} KirinAttackNavigationV2;
/* ALL six-second cohort before readiness filtering; producer keys only. Never substitute keys
 * from legacy event batches. Source/authority races and all failures preserve output bytes. */
uint8_t kirin_hypha_poll_attack_navigation_v2(const KirinHypha*, uint32_t request_size,
    const KirinAttackNavigationRequestV2*, uint32_t out_size, KirinAttackNavigationV2*);
typedef struct {
    uint32_t version, struct_size;
    KirinMeterSession session;
    uint64_t processed_frames, pending_frames;
    uint8_t summary_status, reserved[7];
} KirinMeterSessionV2;
enum { KIRIN_SESSION_SUMMARY_EMPTY=0, KIRIN_SESSION_SUMMARY_COMPLETE=1, KIRIN_SESSION_SUMMARY_PENDING_TAIL=2 };
/* Frames per channel. Values refer to processed_frames; pending_frames have not reached EBU.
 * A paused pending tail makes Max TP a confirmed lower bound; it does not supply complete PLR. */
uint8_t kirin_hypha_poll_meter_session_v2(const KirinHypha*, uint32_t version,
    uint32_t out_size, KirinMeterSessionV2*);
#ifdef __cplusplus
}
#endif
#endif
