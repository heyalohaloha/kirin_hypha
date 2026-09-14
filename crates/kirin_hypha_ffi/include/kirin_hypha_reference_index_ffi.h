#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct KirinReferenceCaptureIndex KirinReferenceCaptureIndex;
typedef struct { uint8_t digest[32]; int16_t bands[8]; float rms[2]; float peak[2]; } KirinReferenceCaptureUnit;
KirinReferenceCaptureIndex* kirin_reference_index_create(uint32_t rate,uint32_t channels,uint32_t frames);
void kirin_reference_index_drop(KirinReferenceCaptureIndex*);
bool kirin_reference_index_push(KirinReferenceCaptureIndex*,const float*,size_t);
uint32_t kirin_reference_index_finish(KirinReferenceCaptureIndex*,KirinReferenceCaptureUnit*);
uint32_t kirin_reference_index_compare(const KirinReferenceCaptureUnit*,const KirinReferenceCaptureUnit*,uint32_t);
#ifdef __cplusplus
}
static_assert(sizeof(KirinReferenceCaptureUnit)==64,"Capture index storage contract");
#endif
