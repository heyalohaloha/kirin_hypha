#ifndef KIRIN_HYPHA_LOCAL_BLIND_CAPTURE_FFI_H
#define KIRIN_HYPHA_LOCAL_BLIND_CAPTURE_FFI_H

#include "kirin_hypha_pair_snapshot_ffi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char request_id[37];
  uint64_t pair_generation;
  uint64_t capture_generation;
  uint64_t clock_generation;
  uint8_t clock_source;
  int64_t clock_position_at_issue;
  uint32_t sample_rate;
  uint32_t channels;
  int64_t native_start;
  int64_t frames;
  int64_t expires_at_unix_ms;
  char pre_project_hash[64];
  char pre_instance_id[64];
} KirinLocalBlindCaptureRequest;

/*
 * The version is part of the link contract.  Do not reuse this symbol after changing either the
 * argument list or KirinLocalBlindCaptureRequest: an older Rust staticlib must fail at link time,
 * rather than interpreting a scalar argument as an output pointer inside the host process.
 */
bool kirin_hypha_issue_local_blind_capture_request_v2(
    KirinHypha* handle, uint64_t capture_generation, uint64_t clock_generation,
    uint8_t clock_source, int64_t clock_position_at_issue,
    int64_t native_start, int64_t frames,
    KirinLocalBlindCaptureRequest* out);
bool kirin_hypha_poll_local_blind_capture_request(
    KirinHypha* handle, KirinLocalBlindCaptureRequest* out);
bool kirin_hypha_ack_local_blind_capture_request(
    KirinHypha* handle, const char* request_id);
bool kirin_hypha_local_blind_capture_is_armed(
    KirinHypha* handle, const char* request_id);

#ifdef __cplusplus
static_assert(sizeof(KirinLocalBlindCaptureRequest) == 240,
              "Local Blind capture request ABI size drift");
}
#endif

#endif
