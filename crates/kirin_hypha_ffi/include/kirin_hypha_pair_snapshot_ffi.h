#ifndef KIRIN_HYPHA_PAIR_SNAPSHOT_FFI_H
#define KIRIN_HYPHA_PAIR_SNAPSHOT_FFI_H

#include "kirin_hypha_ffi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Local Blind capture authority. Human names and host-specific context are diagnostic only. */
typedef struct {
  uint64_t pair_generation;
  char project_hash[64];
  char pre_instance_id[64];
} KirinExactPairBinding;

/* Returns false without changing out unless one exact, currently owned POST->PRE pair is stable. */
bool kirin_hypha_get_local_blind_pair_binding(KirinHypha* handle,
                                               KirinExactPairBinding* out);

#ifdef __cplusplus
}
#endif

#endif
