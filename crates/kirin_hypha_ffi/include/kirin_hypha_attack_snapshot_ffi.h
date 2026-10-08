#ifndef KIRIN_HYPHA_ATTACK_SNAPSHOT_FFI_H
#define KIRIN_HYPHA_ATTACK_SNAPSHOT_FFI_H
#include <stdint.h>
#include <stdbool.h>
#include "kirin_hypha_snapshot_types.h"
/* The existing raw ALL detail layout is reused inside a new independently sized packet. */
typedef struct KirinHypha KirinHypha;
#include "kirin_hypha_attack_ffi.h"
#define KIRIN_ATTACK_SINGLE_V2_VERSION 2u

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint8_t target;
    uint8_t band;
    uint8_t reserved[6];
    KirinSnapshotEventKey event;
} KirinAttackSingleV2Request;

typedef struct {
    KirinSnapshotHeader header;
    KirinSnapshotEventKey event;
    uint64_t request_token;
    uint64_t measurement_revision;
    uint8_t finish;
    uint8_t reason;
    uint8_t pair_kind;
    uint8_t has_all_pre;
    uint8_t has_all_post;
    uint8_t reserved[3];
    /* BAND: DELAY, ATT, REL, LEVEL. ALL raw details have separate availability flags. */
    KirinSnapshotScalarEvidence lanes[4];
    /* HEAD[96] followed by TAIL[64]; values only at corresponding valid mask=1. */
    double pre[160];
    double post[160];
    uint8_t pre_valid[160];
    uint8_t post_valid[160];
    KirinAttackDetail all_pre;
    KirinAttackDetail all_post;
} KirinAttackSingleSnapshotV2;

#ifdef __cplusplus
extern "C" {
#endif
/* Unknown versions with a readable aligned uint32_t prefix and request_size >= 4
 * return UNSUPPORTED before requiring the V2 request layout. Failure preserves out_token. */
uint8_t kirin_hypha_request_attack_single_v2(const KirinHypha* handle, uint32_t request_size,
    const KirinAttackSingleV2Request* request, uint64_t* out_token);
/* Contention returns BUSY, preserves every output byte and keeps the selected request
 * available for retry. A missing/replaced token returns RETIRED; source loss is a retired snapshot. */
uint8_t kirin_hypha_poll_attack_single_v2(const KirinHypha* handle, uint64_t token,
    uint32_t out_size, KirinAttackSingleSnapshotV2* out);
uint8_t kirin_hypha_cancel_attack_single_v2(const KirinHypha* handle, uint64_t token);
#ifdef __cplusplus
}
#endif
#endif
