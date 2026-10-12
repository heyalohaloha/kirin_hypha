#ifndef KIRIN_HYPHA_OPTIONAL_ANALYSIS_CLOCK_H
#define KIRIN_HYPHA_OPTIONAL_ANALYSIS_CLOCK_H
/* Additive paired analysis ingress. No saved-state, Watch, Record or presentation-latency ABI change. */
#ifdef __cplusplus
extern "C" {
#endif
/* Prepare-thread only, with audio suspended. Enable only for an AAX wrapper; each block still needs its proof.
 * Configuration grants no block timing proof. */
void kirin_hypha_set_aax_analysis_ingress(KirinHypha* handle, bool enabled, bool post);
/* Audio Thread only, after timing preparation and the admitted original-input transaction.
 * Supply that callback's original interleaved scratch once, before it is reused.
 * Local proof: exact qualified Native AddClock, continuous project/engine movement.
 * Paired proof: exact PRE lifetime/generation, current POST callback and verified K; start is
 * POST AddClock minus K. A POST SHARP/PSB difference requires paired proof. Absolute LIVE/PSB
 * uses its ordinary ingress; PRE publication uses local proof. Revisions change with proof identity and never wrap
 * (1 <= revision < 2^62). Missing proof revokes optional facts and prepares fresh analysis.
 * No allocation, lock, I/O, audio output mutation, Watch/Record advance or inferred latency. */
bool kirin_hypha_push_aax_analysis(KirinHypha* handle, const float* interleaved,
    size_t num_frames, uint32_t num_channels,
    bool local_valid, int64_t local_start, uint64_t local_revision,
    bool paired_valid, int64_t paired_start, uint64_t paired_revision);
/* Certified VST3 content clocks use the same deferred original-input transaction for FREQ
 * and paired SHARP/PSB. Configure before callbacks; no certificate is inferred from latency.
 * Local = PRE continuous content time; paired = POST continuous time minus proven K.
 * A content proof revision must be in [1, 2^61). Absolute analysis keeps its ordinary route. */
void kirin_hypha_set_content_analysis_ingress(KirinHypha* handle, bool post);
/* AU FREQ/Perceptual: local = exact render time with an independent project witness;
 * paired = POST render time minus the same callback's LISTEN K (which may be negative
 * for different render origins). Revisions are in [1, 2^60). Configure with audio suspended.
 * Without paired proof (POST) or local proof (PRE), FREQ uses the admitted capture window's
 * own project/render position as an unjoinable local observation, without a proof revision.
 * PRE never publishes that local observation for a difference.
 * No host certificate, latency inference, Record or saved-state change. */
void kirin_hypha_set_audio_unit_analysis_ingress(KirinHypha* handle, bool post);
bool kirin_hypha_push_paired_analysis(KirinHypha* handle, const float* interleaved,
    size_t num_frames, uint32_t num_channels,
    bool local_valid, int64_t local_start, uint64_t local_revision,
    bool paired_valid, int64_t paired_start, uint64_t paired_revision);
#ifdef __cplusplus
}
#endif
#endif
