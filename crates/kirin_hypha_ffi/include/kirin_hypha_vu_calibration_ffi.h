#ifndef KIRIN_HYPHA_VU_CALIBRATION_FFI_H
#define KIRIN_HYPHA_VU_CALIBRATION_FFI_H
#include "kirin_hypha_ffi.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Non-RT, no filesystem I/O. Complete resolved project + instance for PRE/unpaired POST;
 * selected POST uses its exact PRE even while absent. Selected but unresolved, mutex busy,
 * invalid/null, overlapping outputs or short buffers return false with BOTH outputs untouched.
 * Each buffer of 65 bytes accommodates every legal 64-byte identity plus its NUL terminator.
 * Success writes only each identity and its terminator; no legacy identity DTO is changed. */
bool kirin_hypha_get_vu_calibration_locator(KirinHypha* handle,
    char* project_out, size_t project_out_len, char* instance_out, size_t instance_out_len);
#ifdef __cplusplus
}
#endif
#endif
