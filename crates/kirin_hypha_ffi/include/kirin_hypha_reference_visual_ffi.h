#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Worker-only, single-owner handles. No function below belongs on the audio callback. */
typedef struct KirinReferenceVisualMeter KirinReferenceVisualMeter;
typedef struct KirinReferenceVisualAdmission KirinReferenceVisualAdmission;
typedef struct {
    uint64_t frames;
    double peak[2], rms[2], short_lufs, crest_db;
    /* 2026-10-04: raw sums and end-of-bin values for Kirin OS's per-hop facts (reference_visual.rs). */
    double true_peak, momentary_lufs, cross, mid, side;
} KirinReferenceVisualBin;
KirinReferenceVisualMeter* kirin_reference_visual_create(uint32_t rate, uint32_t channels);
void kirin_reference_visual_drop(KirinReferenceVisualMeter*);
bool kirin_reference_visual_push(KirinReferenceVisualMeter*, const float*, size_t sample_count);
bool kirin_reference_visual_finish(KirinReferenceVisualMeter*, KirinReferenceVisualBin*);
KirinReferenceVisualAdmission* kirin_reference_visual_admission_create(void);
bool kirin_reference_visual_admission_set(KirinReferenceVisualAdmission*, bool);
void kirin_reference_visual_admission_drop(KirinReferenceVisualAdmission*);
#ifdef __cplusplus
}
#endif
