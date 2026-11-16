#pragma once
#include <stdint.h>
#include "kirin_hypha_channels.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Non-RT: -1 = unavailable, 0 = no notice, 1 = saved pair waiting for audio. */
int32_t kirin_hypha_record_wav_binding_status(const KirinHypha* handle);
#ifdef __cplusplus
}
#endif
