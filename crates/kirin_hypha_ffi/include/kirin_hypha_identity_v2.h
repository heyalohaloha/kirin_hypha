#ifndef KIRIN_HYPHA_IDENTITY_V2_H
#define KIRIN_HYPHA_IDENTITY_V2_H
#include "kirin_hypha_ffi.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Additive, complete path identity. The legacy KirinIdentity ABI remains unchanged.
 * Legal path components contain up to 64 ASCII bytes plus their null terminator.
 * name retains the legacy display-only buffer. This is a non-RT getter. */
typedef struct {
  char instance_id[65];
  char project_uuid[65];
  char daw_session_uuid[65];
  char name[64];
} KirinIdentityV2;

/* False leaves out unchanged: null, unresolved, busy, invalid, or failed identity claim.
 * Use only a true, complete readback to publish shell transports or save resolved identity. */
bool kirin_hypha_get_identity_v2(KirinHypha* handle, KirinIdentityV2* out);

typedef struct {
  char instance_id[65];
  char name[64];
  uint8_t has_name;
} KirinPreCandidateV2;
typedef struct {
  char instance_id[65];
  char pair_pre_name[64];
  uint8_t has_pair_pre_name;
  char paired_pre_instance_id[65];
  uint8_t has_paired_pre_instance_id;
} KirinPostPairClaimV2;
size_t kirin_hypha_enumerate_pre_candidates_v2(KirinHypha*, KirinPreCandidateV2*, size_t cap);
size_t kirin_hypha_enumerate_post_pair_claims_v2(KirinHypha*, KirinPostPairClaimV2*, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
