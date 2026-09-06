#pragma once
#include "kirin_hypha_ffi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Display-only PSB, twenty 1.2 Bark groups spanning 0–24 Bark. Record is unchanged. */
typedef struct KirinPsbView {
    uint8_t status, has_data, is_delta, channels;
    uint32_t sample_rate, aperture_samples, reserved;
    int64_t presentation_end_samples, state_epoch_samples;
    double shares[20];
} KirinPsbView;

bool kirin_hypha_poll_psb(KirinHypha* handle, KirinPsbView* out);
/* Producer-owned session maximum, not GUI-observed playback-pass maximum. */
bool kirin_hypha_poll_meter_display(KirinHypha* handle, KirinWatchDisplay* out);

#ifdef __cplusplus
}
static_assert(sizeof(KirinPsbView) == 192, "PSB ABI drift");
#endif
