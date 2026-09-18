#ifndef KIRIN_HYPHA_METER_SESSION_FFI_H
#define KIRIN_HYPHA_METER_SESSION_FFI_H

/* KirinMeterSession の C ABI.
 *
 * 追加は必ず末尾へ行う。既存フィールドの offset を動かさないことが互換の条件であり、
 * Rust 側の meter_session_abi_tests.rs の offset 表と、JUCE 側の static_assert が
 * 同時に守っている。 */

/* Record/Keepから独立した常設メーターセッション。値なしはNaN。
 * current/session値はobserved_framesの同一100ms境界から生成される。 */
typedef struct {
  uint64_t generation;
  uint64_t active_frames;   /* 受理したActive音声の総フレーム数 */
  uint64_t observed_frames; /* 全指標が共有する100ms測定境界 */
  uint32_t sample_rate;
  uint8_t state;            /* KIRIN_METER_SESSION_* */
  uint8_t reserved[3];
  double lufs_m;
  double lufs_s;
  double lufs_i;
  double lra;
  double true_peak;
  double max_true_peak;
  double plr;
  uint8_t channels;
  uint8_t balance_state; /* KIRIN_BALANCE_* */
  uint8_t channel_clip_latched[2], stereo_reserved[4]; /* VU表示のみ。Session clip_eventsとは独立 */
  double sample_peak_dbfs[2];
  double sample_peak_hold_dbfs[2];
  double channel_true_peak_dbtp[2];
  double channel_max_true_peak_dbtp[2];
  uint64_t clip_events[2];
  double balance_db;  /* positive=L, negative=R; one-sidedはbalance_stateで表す */
  double correlation; /* fixed 3 s; denominator 0/mono/未成立はNaN */
  uint8_t field_size; /* 0=unavailable, otherwise KIRIN_STEREO_FIELD_SIZE */
  uint8_t field_observation_count; /* rolling 100 ms observations, maximum 30 */
  uint8_t field_reserved[6];
  uint8_t field_density[KIRIN_STEREO_FIELD_BINS]; /* rolling 3 s MID/SIDE density */
  double max_lufs_m; /* EBU Mode Maximum Momentary through observed_frames */
  double channel_vu_dbfs[2], channel_instant_true_peak_dbtp[2]; /* 300 ms VU; 100 ms TP */
  /* MONO: how much of each third-octave band survives the mono sum, from the latest exact
   * observation. 0 dB keeps everything, -3.01 dB is a hard-panned source, and the floor is a band
   * that vanishes. NaN is a band with nothing to measure, never a band that reads 0 dB. */
  uint8_t mono_sum_band_count;  /* 0 = unavailable, KIRIN_MONO_SUM_BAND_COUNT = available.
                                 * A flag, not a variable band count. */
  uint8_t mono_sum_reserved[3];
  float mono_sum_approximate_below_hz; /* fewer than three cycles below this; 0 when unavailable */
  float mono_sum_db[KIRIN_MONO_SUM_BAND_COUNT];
} KirinMeterSession;

#endif
