#ifndef KIRIN_HYPHA_CHAIN_OBSERVATION_H
#define KIRIN_HYPHA_CHAIN_OBSERVATION_H
#include <stdint.h>
#include <stdbool.h>
#include "kirin_hypha_ffi.h"
#define KIRIN_CHAIN_VERSION 1u
#define KIRIN_CHAIN_VERSION_LATEST 2u
#define KIRIN_CHAIN_CAPACITY 600u
#define KIRIN_CHAIN_UNAVAILABLE 0u
#define KIRIN_CHAIN_SYNCING 1u
#define KIRIN_CHAIN_ACTIVE 2u
#define KIRIN_CHAIN_HOLD 3u
#define KIRIN_CHAIN_AMBIGUOUS 4u
#define KIRIN_CHAIN_SUPPRESSED 5u
#define KIRIN_CHAIN_CLOCK_POLICY_UNKNOWN 0u
#define KIRIN_CHAIN_CLOCK_POLICY_STUDIO_PRO_812_WINDOWS_VST3 1u

/* Same complete 400 ms aperture. NaN is missing, never zero. Window endpoint, not peak sample. */
typedef struct {
    uint64_t pre_epoch, post_epoch, pre_incarnation, pre_generation, post_generation;
    uint64_t pre_run, post_run, pre_observed, post_observed;
    int64_t endpoint;
    double pre_m, post_m, pre_tp, post_tp, delta_m, delta_tp, relation;
    uint8_t source, crossing, pre_severity, post_severity;
    uint8_t reserved[4];
} KirinChainPoint;
typedef struct {
    uint64_t revision, binding, post_observed;
    uint32_t version, sample_rate, count;
    uint8_t status, reserved[3];
} KirinChainSnapshot;
#ifdef __cplusplus
extern "C" {
#endif
/* Non-RT setup before endpoint publication. Unknown and unrecognized values cannot admit
   exact PRE/POST comparison; this never changes the absolute Meter Session. */
void kirin_hypha_set_chain_clock_policy(KirinHypha* handle, uint8_t policy);
/* UI only. v1 requires 600 slots; v2 accepts 1 (latest-only) or 600 slots.
   False leaves outputs untouched (including unchanged revision). */
bool kirin_hypha_poll_chain_observation(KirinHypha* handle, uint32_t version,
    uint64_t known_revision, KirinChainSnapshot* out, KirinChainPoint* points, uint32_t capacity);
#ifdef __cplusplus
}
static_assert(sizeof(KirinChainSnapshot) == 40);
static_assert(sizeof(KirinChainPoint) == 144);
#endif
#endif
