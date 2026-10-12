#ifndef KIRIN_HYPHA_OPTIONAL_ANALYSIS_CLOCK_H
#define KIRIN_HYPHA_OPTIONAL_ANALYSIS_CLOCK_H
/* Additive AAX analysis ingress. No saved-state, Watch, Record or presentation-latency ABI change. */
#ifdef __cplusplus
extern "C" {
#endif
/* Prepare-thread only, with audio suspended. Enable only for a certified AAX host/rate.
 * Configuration grants no block timing proof. */
void kirin_hypha_set_aax_analysis_ingress(KirinHypha* handle, bool enabled, bool post);
/* Audio Thread only, after timing preparation and the admitted original-input transaction.
 * Supply that callback's original interleaved scratch once, before it is reused.
 * Local proof: exact qualified Native AddClock, continuous project/engine movement.
 * Paired proof: exact PRE lifetime/generation, current POST callback and verified K; start is
 * POST AddClock minus K. A POST SHARP/PSB difference requires paired proof. Absolute LIVE/PSB
 * and PRE publication use local proof. Revisions change with proof identity and never wrap
 * (1 <= revision < 2^62). Missing proof revokes optional facts and prepares fresh analysis.
 * No allocation, lock, I/O, audio output mutation, Watch/Record advance or inferred latency. */
bool kirin_hypha_push_aax_analysis(KirinHypha* handle, const float* interleaved,
    size_t num_frames, uint32_t num_channels,
    bool local_valid, int64_t local_start, uint64_t local_revision,
    bool paired_valid, int64_t paired_start, uint64_t paired_revision);
#ifdef __cplusplus
}
#endif
#endif
