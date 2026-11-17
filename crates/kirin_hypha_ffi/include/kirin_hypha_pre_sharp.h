#pragma once
#include "kirin_hypha_ffi.h"
#ifdef __cplusplus
extern "C" {
#endif
/* PRE own observation. UI/non-RT only; no PRE/POST delta or transport payload. */
typedef struct KirinPreSharpFrame {
    uint32_t version, struct_size;
    uint64_t owner_generation, analysis_generation;
    int64_t state_epoch_samples, presentation_end_samples;
    uint32_t sample_rate, aperture_samples, channels, reserved;
    double sharpness_acum;
} KirinPreSharpFrame;
/* token=0 acquires; nonzero renews only that live token. 0 means unavailable/busy. */
uint64_t kirin_hypha_request_pre_sharp(KirinHypha* handle, uint64_t token);
bool kirin_hypha_poll_pre_sharp(KirinHypha* handle, uint64_t token, KirinPreSharpFrame* out);
/* false is busy/foreign; retry busy release. Unrenewed demand expires in 1.5 seconds. */
bool kirin_hypha_release_pre_sharp(KirinHypha* handle, uint64_t token);
#ifdef __cplusplus
}
static_assert(sizeof(KirinPreSharpFrame) == 64, "PRE sharp frame ABI");
#endif
