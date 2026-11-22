#ifndef KIRIN_HYPHA_LIVE_MATCH_SPAN_H
#define KIRIN_HYPHA_LIVE_MATCH_SPAN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct KirinLiveMatchSpan KirinLiveMatchSpan;
typedef struct KirinLiveMatchSpanFacts {
    double pre_lufs, post_lufs, pre_peak, post_peak;
} KirinLiveMatchSpanFacts;
KirinLiveMatchSpan* kirin_hypha_live_match_span_create(uint32_t rate, uint32_t channels);
void kirin_hypha_live_match_span_destroy(KirinLiveMatchSpan*);
bool kirin_hypha_live_match_span_feed(KirinLiveMatchSpan*, const float* pre, const float* post, size_t frames);
bool kirin_hypha_live_match_span_measure(const KirinLiveMatchSpan*, KirinLiveMatchSpanFacts*);
#ifdef __cplusplus
}
#endif
#endif
