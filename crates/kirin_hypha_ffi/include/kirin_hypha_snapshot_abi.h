#ifndef KIRIN_HYPHA_SNAPSHOT_ABI_H
#define KIRIN_HYPHA_SNAPSHOT_ABI_H

#include <stdbool.h>
#include <stdint.h>
#include "kirin_hypha_snapshot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Independent from the legacy, unsized KirinAbiContract. No old struct grows.
 * The shell validates this contract before creating an engine. A static library
 * without the sized entry point cannot be linked into the new shell. */
#define KIRIN_SNAPSHOT_ABI_VERSION 1u
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t legacy_revision;
    uint32_t drum_cohort_capacity;
    uint32_t time_raw_capacity;
    uint32_t time_history_capacity;
    uint32_t summary_version;
    uint32_t single_version;
    uint32_t time_version;
    uint32_t reserved;
    uint64_t summary_size;
    uint64_t summary_align;
    uint64_t single_size;
    uint64_t single_align;
    uint64_t time_size;
    uint64_t time_align;
    /* Canonical offset order is pinned independently in C and Rust tests. */
    uint32_t common_offsets[12];
    uint32_t summary_offsets[8];
    uint32_t single_offsets[10];
    uint32_t time_offsets[8];
    uint32_t enum_revision;
    uint32_t layout_reserved;
} KirinSnapshotAbiContract;

/* Returns the common snapshot status. Unknown version is Unsupported; short
 * buffer, NULL or misalignment is InvalidRequest. These leave all output unchanged.
 * Callers provide writable storage of out_size bytes. Never call the old
 * unsized entry point with a buffer for this independent contract. */
uint8_t kirin_hypha_snapshot_abi_contract(uint32_t version, uint32_t out_size,
                                     KirinSnapshotAbiContract* out);

#ifdef __cplusplus
}
#endif
#endif
