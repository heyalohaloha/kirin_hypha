#ifndef KIRIN_HYPHA_LEVEL_SNAPSHOT_H
#define KIRIN_HYPHA_LEVEL_SNAPSHOT_H

#include <stddef.h>
#include "kirin_hypha_chain_observation.h"

#define KIRIN_LEVEL_SNAPSHOT_VERSION 1u

/* UI-only: one POST cutoff for frame, absolute 10 Hz history and exact-pair chain.
   chain_updated=0 means the known revision is unchanged; retain the prior chain snapshot.
   False leaves all outputs untouched. No legacy wire or partial mixed-version fallback. */
typedef struct {
    uint32_t version, history_count;
    uint8_t chain_updated, reserved[7];
    KirinObservatoryFrame frame;
    KirinChainSnapshot chain;
} KirinLevelSnapshot;

#ifdef __cplusplus
extern "C" {
#endif
bool kirin_hypha_poll_level_snapshot(
    KirinHypha* handle, uint32_t version, uint32_t history_max,
    KirinMeterHistoryEntry* history, uint32_t history_capacity,
    uint64_t known_chain_revision, KirinChainPoint* chain, uint32_t chain_capacity,
    KirinLevelSnapshot* out);
#ifdef __cplusplus
}
static_assert (sizeof (KirinLevelSnapshot) == 2160);
static_assert (offsetof (KirinLevelSnapshot, frame) == 16);
static_assert (offsetof (KirinLevelSnapshot, chain) == 2120);
#endif

#endif
