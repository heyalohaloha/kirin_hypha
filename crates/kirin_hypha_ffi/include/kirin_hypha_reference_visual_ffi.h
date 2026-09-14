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
typedef struct KirinReferenceTonalMeter KirinReferenceTonalMeter;
typedef struct {
    uint64_t frames;
    double peak[2], rms[2], short_lufs, crest_db;
} KirinReferenceVisualBin;
typedef struct {
    uint32_t sample_rate, channels;
    uint64_t epoch, frames_seen;
    uint32_t fft_size[3], hop_samples[3];
    uint64_t window_end_samples[3];
    float values_db[60];
    uint64_t valid_bits;
} KirinReferenceTonalSnapshot;
KirinReferenceVisualMeter* kirin_reference_visual_create(uint32_t rate, uint32_t channels);
void kirin_reference_visual_drop(KirinReferenceVisualMeter*);
bool kirin_reference_visual_push(KirinReferenceVisualMeter*, const float*, size_t sample_count);
bool kirin_reference_visual_finish(KirinReferenceVisualMeter*, KirinReferenceVisualBin*);
KirinReferenceTonalMeter* kirin_reference_tonal_create(uint32_t rate, uint32_t channels);
void kirin_reference_tonal_drop(KirinReferenceTonalMeter*);
bool kirin_reference_tonal_push(KirinReferenceTonalMeter*, const float*, size_t sample_count);
bool kirin_reference_tonal_snapshot(const KirinReferenceTonalMeter*, KirinReferenceTonalSnapshot*);
bool kirin_reference_tonal_reset(KirinReferenceTonalMeter*);
size_t kirin_reference_tonal_allocated_bytes(const KirinReferenceTonalMeter*);
KirinReferenceVisualAdmission* kirin_reference_visual_admission_create(void);
bool kirin_reference_visual_admission_set(KirinReferenceVisualAdmission*, bool);
void kirin_reference_visual_admission_drop(KirinReferenceVisualAdmission*);
#ifdef __cplusplus
}
#endif
