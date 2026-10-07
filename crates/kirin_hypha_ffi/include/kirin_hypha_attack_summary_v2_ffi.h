#ifndef KIRIN_HYPHA_ATTACK_SUMMARY_V2_FFI_H
#define KIRIN_HYPHA_ATTACK_SUMMARY_V2_FFI_H
#include "kirin_hypha_snapshot_types.h"
#include "kirin_hypha_channels.h"
#ifdef __cplusplus
extern "C" {
#endif
#define KIRIN_ATTACK_BAND_SUMMARY_V2_VERSION 2u
#define KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY 8u
#define KIRIN_ATTACK_BAND_SUMMARY_V2_HEAD 96u
#define KIRIN_ATTACK_BAND_SUMMARY_V2_TAIL 64u
#define KIRIN_RENDER_WHOLE_POINT 0u
#define KIRIN_RENDER_WHOLE_INTERVAL 1u
#define KIRIN_RENDER_CONFIRMED_SUBSET 2u
#define KIRIN_RENDER_NO_SCALAR 3u

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint8_t target;
    uint8_t band;
    uint8_t reserved[6];
} KirinAttackBandSummaryV2Request;

typedef struct {
    uint8_t class_count[5];
    uint8_t cohort_count;
    uint8_t whole_median_available;
    uint8_t whole_numeric_informative;
    KirinSnapshotInterval whole_interval;
    double exact_median;
    int64_t exact_latest_event_sample;
    double resolution;
    uint8_t reason_count[KIRIN_REASON_CAPACITY];
    uint8_t exact_count;
    uint8_t render_kind;
    uint8_t whole_within_resolution;
    uint8_t reserved[5];
} KirinAttackBandLaneSummaryV2;

typedef struct {
    uint8_t participating_bits;
    uint8_t valid_count;
    uint8_t connect_previous;
    uint8_t has_pre;
    uint8_t reserved[4];
    double pre_mean;
    double pre_min;
    double pre_max;
    double post_mean;
    double post_min;
    double post_max;
} KirinAttackBandAveragePointV2;

typedef struct {
    KirinSnapshotHeader header;
    uint8_t cohort_count;
    uint8_t reserved[7];
    KirinSnapshotEventKey events[KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY];
    uint8_t pair_kind[KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY];
    KirinSnapshotScalarEvidence evidence[KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY][4];
    KirinAttackBandLaneSummaryV2 lanes[4];
    KirinAttackBandAveragePointV2 head[KIRIN_ATTACK_BAND_SUMMARY_V2_HEAD];
    KirinAttackBandAveragePointV2 tail[KIRIN_ATTACK_BAND_SUMMARY_V2_TAIL];
} KirinAttackBandSummaryV2;

/* request_size and out_size are checked before either structure is accessed.
 * Null, unknown enum/version, short buffer, contention and identity races leave
 * all output bytes unchanged. Success may contain Pending, Unknown or N/A.
 * Cohort keys are selected before measurement lookup; there is no backfill. */
uint8_t kirin_hypha_poll_attack_band_summary_v2(const KirinHypha* handle,
    uint32_t request_size, const KirinAttackBandSummaryV2Request* request,
    uint32_t out_size, KirinAttackBandSummaryV2* out);
#ifdef __cplusplus
}
#endif
#endif
