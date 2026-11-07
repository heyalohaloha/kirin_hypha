#pragma once
#include "kirin_hypha_ffi.h"

// An ended A/B still draining its return keeps the project's capture exclusion on the POST's
// replacement engine. Attach consumes the hold; a null engine releases it.
extern "C"
{
typedef struct KirinHyphaLocalBlindHold KirinHyphaLocalBlindHold;
KirinHyphaLocalBlindHold* kirin_hypha_detach_local_blind (KirinHypha* handle, uint64_t scope_epoch);
bool kirin_hypha_attach_local_blind (KirinHypha* handle, KirinHyphaLocalBlindHold* hold);
}
