#ifndef KIRIN_HYPHA_LOCAL_BLIND_RESULT_FFI_H
#define KIRIN_HYPHA_LOCAL_BLIND_RESULT_FFI_H

#include "kirin_hypha_local_blind_capture_ffi.h"

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
  int64_t start;
  int64_t frames;
  uint64_t sample_count;
  char pcm_sha256[65];
} KirinLocalBlindPreCaptureReceipt;

bool kirin_hypha_publish_local_blind_pre_capture(
    KirinHypha* handle, const char* request_id, const float* interleaved,
    size_t sample_count, KirinLocalBlindPreCaptureReceipt* out_receipt);
bool kirin_hypha_read_local_blind_pre_capture(
    KirinHypha* handle, const char* request_id, float* out_interleaved,
    size_t sample_count, KirinLocalBlindPreCaptureReceipt* out_receipt);
bool kirin_hypha_ack_local_blind_pre_capture(
    KirinHypha* handle, const KirinLocalBlindPreCaptureReceipt* receipt);
bool kirin_hypha_local_blind_pre_capture_was_consumed(
    KirinHypha* handle, const KirinLocalBlindPreCaptureReceipt* receipt);
bool kirin_hypha_retire_local_blind_pre_capture(
    KirinHypha* handle, const char* request_id);

#ifdef __cplusplus
}
#endif

#endif
