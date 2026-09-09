#ifndef KIRIN_HYPHA_REFERENCE_FFI_H
#define KIRIN_HYPHA_REFERENCE_FFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reference Blindのsample-aligned A/Bからworker threadで確定する整数Gain facts. */
typedef struct {
  uint64_t paired_block_count;
  int64_t paired_loudness_delta_median_millilu;
  int64_t a_cue_true_peak_millidbtp;
  int64_t b_cue_true_peak_millidbtp;
} KirinReferenceGainFacts;

typedef struct {
  uint64_t paired_window_count;
  int64_t paired_energy_delta_millidb;
  int64_t post_cue_true_peak_millidbtp;
  int64_t pre_cue_true_peak_millidbtp;
} KirinTrackEventGainFacts;

/* 400ms / 100ms hopの連続active blockとCue True PeakをBS.1770核で解析する。
 * worker thread専用。A/Bは同一sample rate/channel/frame countのinterleaved f32。 */
bool kirin_hypha_analyze_reference_gain(const float* a, const float* b,
                                        size_t num_frames, uint32_t sample_rate,
                                        uint32_t num_channels,
                                        KirinReferenceGainFacts* out);

/* TRACK/STEMのexact 4秒PCMを20ms paired event energy windowで解析する別名policy。
 * 各側最大eventの-40dB以内を使う。短音を反復せず、十分な実信号がなければfalse。 */
bool kirin_hypha_analyze_track_event_gain(const float* post, const float* pre,
                                          size_t num_frames, uint32_t sample_rate,
                                          uint32_t num_channels,
                                          KirinTrackEventGainFacts* out);

#ifdef __cplusplus
}
#endif

#endif
