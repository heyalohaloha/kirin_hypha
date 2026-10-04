#pragma once
#include "kirin_hypha_reference_visual_ffi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct KirinHypha KirinHypha;
bool kirin_hypha_set_version_blind_capture_exclusion(KirinHypha*, bool);
bool kirin_hypha_set_reference_capture_active(KirinHypha*, bool);
#ifdef __cplusplus
}
#endif
