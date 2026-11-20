#pragma once
#include "kirin_hypha_ffi.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Reserved unsupported ABI. PRE own SHARP is deferred after CPU-budget failure.
 * These functions never access an engine, acquire demand, or start a worker. */
typedef struct KirinPreSharpFrame {
    uint32_t version, struct_size;
    uint64_t owner_generation, analysis_generation;
    int64_t state_epoch_samples, presentation_end_samples;
    uint32_t sample_rate, aperture_samples, channels, reserved;
    double sharpness_acum;
} KirinPreSharpFrame;
/* Always returns 0 (unsupported), for every handle/token. */
uint64_t kirin_hypha_request_pre_sharp(KirinHypha* handle, uint64_t token);
/* Always false; out remains untouched. */
bool kirin_hypha_poll_pre_sharp(KirinHypha* handle, uint64_t token, KirinPreSharpFrame* out);
/* Always false; no PRE-local demand exists in this build. */
bool kirin_hypha_release_pre_sharp(KirinHypha* handle, uint64_t token);
#ifdef __cplusplus
}
static_assert(sizeof(KirinPreSharpFrame) == 64, "PRE sharp frame ABI");
#endif
