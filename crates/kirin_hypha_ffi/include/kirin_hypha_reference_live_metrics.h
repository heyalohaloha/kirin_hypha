#ifndef KIRIN_HYPHA_REFERENCE_LIVE_METRICS_H
#define KIRIN_HYPHA_REFERENCE_LIVE_METRICS_H
#include <stdbool.h>
#include "kirin_hypha_channels.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* One native Session observation. NaN is missing. No audio or Record changes.
 * PRE values are formed only from a same-snapshot, confirmed exact-pair difference.
 * False leaves output untouched; callers must not extend an old value's lifetime.
 * UI/non-RT thread only. */
typedef struct {
    uint32_t version, struct_size, sample_rate, channels;
    uint64_t measurement_epoch, generation, observed_frames;
    double own_crest_db, own_psr_db, pre_crest_db, pre_psr_db;
    uint32_t valid_mask, reserved; /* bits 0..3 in the double field order */
} KirinReferenceLiveMetricFrame;
bool kirin_hypha_poll_reference_live_metrics(KirinHypha*, KirinReferenceLiveMetricFrame*);
#ifdef __cplusplus
}
static_assert(sizeof(KirinReferenceLiveMetricFrame) == 80, "Reference live frame ABI");
static_assert(offsetof(KirinReferenceLiveMetricFrame, own_crest_db) == 40, "Reference live offset");
#endif
#endif
