#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
struct KirinHypha;
typedef struct KirinReferenceAnalysisOwner KirinReferenceAnalysisOwner;
typedef struct KirinReferenceAnalysisGrant KirinReferenceAnalysisGrant;
KirinReferenceAnalysisOwner* kirin_reference_analysis_create(void);
KirinReferenceAnalysisOwner* kirin_hypha_reference_analysis_owner(const struct KirinHypha*);
KirinReferenceAnalysisGrant* kirin_reference_analysis_acquire(const KirinReferenceAnalysisOwner*);
bool kirin_reference_analysis_same(const KirinReferenceAnalysisOwner*,const KirinReferenceAnalysisOwner*);
void kirin_reference_analysis_owner_drop(KirinReferenceAnalysisOwner*);
void kirin_reference_analysis_grant_drop(KirinReferenceAnalysisGrant*);
#ifdef __cplusplus
}
#endif
