#ifndef KIRIN_HYPHA_PAIR_PREVIEW_FFI_H
#define KIRIN_HYPHA_PAIR_PREVIEW_FFI_H
#include "kirin_hypha_ffi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct KirinPairPreview KirinPairPreview;
typedef struct KirinPairPreviewValue {
    uint64_t generation;
    uint8_t complete;
    uint8_t has_single;
    KirinPreCandidate candidate;
} KirinPairPreviewValue;
KirinPairPreview* kirin_hypha_pair_preview_create(KirinHypha*);
bool kirin_hypha_pair_preview_matches(KirinHypha*, const KirinPairPreview*);
bool kirin_hypha_pair_preview_request(const KirinPairPreview*);
bool kirin_hypha_pair_preview_poll(const KirinPairPreview*, KirinPairPreviewValue*);
void kirin_hypha_pair_preview_cancel(const KirinPairPreview*);
void kirin_hypha_pair_preview_destroy(KirinPairPreview*);
void kirin_hypha_pair_preview_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
