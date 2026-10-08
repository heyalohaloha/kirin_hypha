#ifndef KIRIN_HYPHA_SNAPSHOT_TYPES_H
#define KIRIN_HYPHA_SNAPSHOT_TYPES_H
#include <stdint.h>

/* Numeric enum codes and layouts are permanent for this ABI revision.
 * Unknown codes are rejected; reserved bytes are zero. No legacy type grows. */
#define KIRIN_SNAPSHOT_SUCCESS 0u
#define KIRIN_SNAPSHOT_BUSY 1u
#define KIRIN_SNAPSHOT_INVALID_REQUEST 2u
#define KIRIN_SNAPSHOT_UNSUPPORTED 3u
#define KIRIN_SNAPSHOT_RETIRED 4u
#define KIRIN_ENDPOINT_FINITE 0u
#define KIRIN_ENDPOINT_NEGATIVE_INFINITY 1u
#define KIRIN_ENDPOINT_POSITIVE_INFINITY 2u
#define KIRIN_INTERVAL_MILLISECONDS 0u
#define KIRIN_INTERVAL_DECIBELS 1u
#define KIRIN_SCALAR_EXACT 0u
#define KIRIN_SCALAR_BOUND 1u
#define KIRIN_SCALAR_UNKNOWN 2u
#define KIRIN_SCALAR_PENDING 3u
#define KIRIN_SCALAR_NOT_APPLICABLE 4u
#define KIRIN_FINISH_ACQUIRING 0u
#define KIRIN_FINISH_FULL 1u
#define KIRIN_FINISH_AUDIO_END 2u
#define KIRIN_FINISH_NOT_KEPT 3u
#define KIRIN_FINISH_RETIRED 4u
#define KIRIN_SNAPSHOT_ALL_LIVE 0u
#define KIRIN_SNAPSHOT_BAND_SUMMARY 1u
#define KIRIN_SNAPSHOT_SINGLE 2u
#define KIRIN_SNAPSHOT_TIME 3u
#define KIRIN_TARGET_POST 0u
#define KIRIN_TARGET_DELTA 1u
#define KIRIN_TARGET_PRE 2u
#define KIRIN_REASON_NONE 0u
#define KIRIN_REASON_NO_PAIR 1u
#define KIRIN_REASON_SILENT 2u
#define KIRIN_REASON_BOTH_SILENT 3u
#define KIRIN_REASON_RINGING 4u
#define KIRIN_REASON_NEXT_HIT 5u
#define KIRIN_REASON_NOT_KEPT 6u
#define KIRIN_REASON_MAPPING 7u
#define KIRIN_REASON_CLOCK 8u
#define KIRIN_REASON_SOURCE_CHANGED 9u
#define KIRIN_REASON_WORKER_UNAVAILABLE 10u
#define KIRIN_REASON_REQUEST_DEADLINE 11u
#define KIRIN_REASON_WAITING_AUDIO 12u
#define KIRIN_REASON_WAITING_SERVICE 13u
#define KIRIN_REASON_WAITING_PUBLICATION 14u
#define KIRIN_REASON_SEMANTICS 15u
#define KIRIN_REASON_LONG_TAIL 16u
#define KIRIN_REASON_AUDIO_END 17u
#define KIRIN_REASON_COUNT 18u
#define KIRIN_REASON_CAPACITY 32u

typedef struct {
    uint8_t incarnation[16];
    uint64_t generation;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t reserved[3];
    uint8_t odf_hash[32];
} KirinSnapshotSourceKey;

typedef struct {
    KirinSnapshotSourceKey source;
    int64_t event_sample;
    uint64_t token;
} KirinSnapshotEventKey;

typedef struct {
    uint8_t kind;
    uint8_t closed;
    uint8_t reserved[6];
    /* Finite only. Infinity carries value=0 and closed=0, never a sentinel NaN. */
    double value;
} KirinSnapshotEndpoint;

typedef struct {
    KirinSnapshotEndpoint lower;
    KirinSnapshotEndpoint upper;
    uint8_t unit;
    uint8_t reserved[7];
} KirinSnapshotInterval;

typedef struct {
    KirinSnapshotInterval interval;
    uint64_t measurement_revision;
    uint64_t proof_revision;
    int64_t requested_start;
    int64_t requested_end;
    int64_t actual_start;
    int64_t actual_end;
    double resolution;
    uint8_t class_code;
    uint8_t reason;
    uint8_t has_interval;
    uint8_t finish;
    uint8_t reserved[4];
} KirinSnapshotScalarEvidence;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint8_t kind;
    uint8_t target;
    uint8_t band;
    uint8_t signal_state;
    uint32_t flags;
    uint64_t snapshot_revision;
    uint64_t authority_revision;
    int64_t cutoff_sample;
    KirinSnapshotSourceKey source;
    uint8_t band_semantic_hash[32];
} KirinSnapshotHeader;
#endif
