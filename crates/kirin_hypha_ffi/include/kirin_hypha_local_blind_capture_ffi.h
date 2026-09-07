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
  uint32_t sample_rate;
  uint32_t channels;
  int64_t pre_start;
  int64_t post_start;
  int64_t frames;
  int64_t expires_at_unix_ms;
  char pre_project_hash[64];
  char pre_instance_id[64];
} KirinLocalBlindCaptureRequest;

bool kirin_hypha_issue_local_blind_capture_request(
    KirinHypha* handle, uint64_t capture_generation, uint64_t clock_generation,
    int64_t pre_start, int64_t post_start, int64_t frames,
    KirinLocalBlindCaptureRequest* out);
bool kirin_hypha_poll_local_blind_capture_request(
    KirinHypha* handle, KirinLocalBlindCaptureRequest* out);
bool kirin_hypha_ack_local_blind_capture_request(
    KirinHypha* handle, const char* request_id);
bool kirin_hypha_local_blind_capture_is_armed(
    KirinHypha* handle, const char* request_id);

#ifdef __cplusplus
}
#endif

#endif
