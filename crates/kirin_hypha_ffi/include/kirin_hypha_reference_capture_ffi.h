#pragma once
#include "kirin_hypha_reference_visual_ffi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct KirinHypha KirinHypha;
bool kirin_hypha_set_version_blind_capture_exclusion(KirinHypha*, bool);
bool kirin_hypha_set_reference_capture_active(KirinHypha*, bool);
KirinReferenceVisualMeter* kirin_reference_capture_create(uint32_t, uint32_t);
bool kirin_reference_capture_finish(KirinReferenceVisualMeter*, KirinReferenceVisualBin*, double*);
bool kirin_reference_capture_totals(KirinReferenceVisualMeter*, double*, double*);
#ifdef __cplusplus
}
#endif
