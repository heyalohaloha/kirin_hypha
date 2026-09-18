#ifndef KIRIN_HYPHA_METER_SESSION_FFI_H
#define KIRIN_HYPHA_METER_SESSION_FFI_H

#include "kirin_hypha_channels.h"

/* KirinMeterSession の C ABI.
 *
 * 追加は必ず末尾へ行う。既存フィールドの offset を動かさないことが互換の条件であり、
 * Rust 側の meter_session_abi_tests.rs の offset 表と、JUCE 側の static_assert が
 * 同時に守っている。
 *
 * **B-958 で入力チャンネル配列を [2] から [KIRIN_MAX_CHANNELS] へ広げた。** これは追加ではなく
 * 既存 offset の移動であり、旧 staticlib と新ヘッダの組合せは全フィールドがずれる。
 * 検出は `kirin_hypha_abi_contract`（実行時）と `KIRIN_OBSERVATORY_FRAME_VERSION`（フレーム単位）
 * が担う。以後 offset を動かす変更は、同じ 2 つを同時に更新すること。
 *
 * `channels` はスロットの有効範囲そのものである。`channels` 以降のスロットは
 * **測定を持たない**（double = NaN / 整数 = 0）ので、表示・保存・集計に入れてはならない。
 * 整数の 0 は `channels` 未満では「clip が無かった」という測定結果であり、
 * 以降では「測っていない」である。両者を分けるのは値ではなく `channels` である。 */

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
  uint8_t channels; /* 有効スロット数。0..KIRIN_MAX_CHANNELS */
  uint8_t balance_state; /* KIRIN_BALANCE_* */
  uint8_t channel_clip_latched[KIRIN_MAX_CHANNELS], stereo_reserved[6]; /* VU表示のみ。Session clip_eventsとは独立 */
  double sample_peak_dbfs[KIRIN_MAX_CHANNELS];
  double sample_peak_hold_dbfs[KIRIN_MAX_CHANNELS];
  double channel_true_peak_dbtp[KIRIN_MAX_CHANNELS];
  double channel_max_true_peak_dbtp[KIRIN_MAX_CHANNELS];
  uint64_t clip_events[KIRIN_MAX_CHANNELS];
  double balance_db;  /* positive=L, negative=R; one-sidedはbalance_stateで表す */
  double correlation; /* fixed 3 s; denominator 0/mono/未成立はNaN */
  uint8_t field_size; /* 0=unavailable, otherwise KIRIN_STEREO_FIELD_SIZE */
  uint8_t field_observation_count; /* rolling 100 ms observations, maximum 30 */
  uint8_t field_reserved[6];
  uint8_t field_density[KIRIN_STEREO_FIELD_BINS]; /* rolling 3 s MID/SIDE density */
  double max_lufs_m; /* EBU Mode Maximum Momentary through observed_frames */
  double channel_vu_dbfs[KIRIN_MAX_CHANNELS];
  double channel_instant_true_peak_dbtp[KIRIN_MAX_CHANNELS]; /* 300 ms VU; 100 ms TP */
  /* MONO: how much of each third-octave band survives the mono sum, from the latest exact
   * observation. 0 dB keeps everything, -3.01 dB is a hard-panned source, and the floor is a band
   * that vanishes. NaN is a band with nothing to measure, never a band that reads 0 dB. */
  uint8_t mono_sum_band_count;  /* 0 = unavailable, KIRIN_MONO_SUM_BAND_COUNT = available.
                                 * A flag, not a variable band count. */
  uint8_t mono_sum_reserved[3];
  /* Fewer than three cycles below this frequency, so the readout marks it approximate. This is
   * the observation layout's own boundary, not a property of what the latest observation held, so
   * it stays valid while mono_sum_band_count is 0 and a held display keeps its marking. It is 0
   * only before the first stereo observation has established a layout. */
  float mono_sum_approximate_below_hz;
  float mono_sum_db[KIRIN_MONO_SUM_BAND_COUNT];
  /* どのチャンネルがどのスロットかを、index ではなく役割で言う（D-5）。
   * slot < channels は KirinChannelRole、それ以降は KIRIN_CHANNEL_ROLE_NONE。 */
  uint8_t channel_positions[KIRIN_MAX_CHANNELS];
  uint8_t layout_id; /* KirinChannelLayoutId。0 = 不明で、レイアウトではない */
  uint8_t layout_reserved[7];
  /* 測定区間。engine が別の layout / sample rate で作られるたびに進む。
   * 区間をまたいだ値を同じ測定として並べないための識別子であり、時刻ではない。 */
  uint64_t measurement_epoch;
} KirinMeterSession;

#endif
