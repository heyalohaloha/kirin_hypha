#ifndef KIRIN_HYPHA_REFERENCE_METRICS_H
#define KIRIN_HYPHA_REFERENCE_METRICS_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* A separate append-only API; no existing plugin struct offsets are changed.
 * NON-RT, single-owner, serialized calls. Does not read files or create threads.
 * Rate/channel mapping must match the live observation being compared.
 * Feed complete 100 ms apertures from one continuous immutable PCM sequence.
 * No zero padding at EOF. NaN means no measurement, not zero. */
typedef struct KirinReferenceMetrics KirinReferenceMetrics;
typedef struct {
    uint64_t end_sample;
    double crest_db;
    double psr_db;
    double sharpness_acum;
} KirinReferenceMetricFrame;
KirinReferenceMetrics* kirin_reference_metrics_create(uint32_t rate, uint32_t channels);
void kirin_reference_metrics_drop(KirinReferenceMetrics* handle);
/* output capacity must be >= 8. `written` and output remain unchanged on failure.
 * sample count = (rate / 10) * channels. Outputs can lag input by resampler latency.
 * A failed engine/panic requires dropping the handle and creating a new analysis.
 * Null/invalid arguments do not advance the input. Caller owns all buffers. */
bool kirin_reference_metrics_push(KirinReferenceMetrics* handle, const float* samples,
    size_t count, KirinReferenceMetricFrame* output, size_t capacity, size_t* written);
/* EOF: release remaining complete level observations with missing SHARP, never
 * pad PCM. Same capacity/ownership/error rules as push. Further push is refused. */
bool kirin_reference_metrics_finish(KirinReferenceMetrics* handle,
    KirinReferenceMetricFrame* output, size_t capacity, size_t* written);
#ifdef __cplusplus
}
static_assert(sizeof(KirinReferenceMetricFrame) == 32, "Reference metric frame ABI");
static_assert(offsetof(KirinReferenceMetricFrame, sharpness_acum) == 24, "Reference metric offset");
#endif
#endif
