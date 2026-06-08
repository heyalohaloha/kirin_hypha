/*
 * kirin_hypha_ffi.h — Kirin Hypha JUCE 移植 Phase 1 の C ABI（正本・手書き）.
 *
 * 対応 staticlib: target/{debug,release}/libkirin_hypha_ffi.a
 * 実装:           crates/kirin_hypha_ffi/src/lib.rs（このヘッダと常に一致させること）.
 *
 * Phase 1 スコープ:
 *   - 実装: create / set_signal_state / push_samples / poll_result / destroy（RT 計測パス）.
 *   - poll_session: symbol のみ・常に false（SessionSummary は Record=Phase 3 でのみ成立）.
 *   - Option<f64> は NaN sentinel で表す（C 側は isnan() で「値なし」を判定）.
 *
 * スレッド契約:
 *   - push_samples: Audio Thread 単独・RT-safe（内部は rtrb push + heartbeat++ のみ）.
 *   - poll_result : UI Thread（内部は try_lock 非ブロッキング）.
 *   - push_samples は毎オーディオブロック呼ぶこと（~200ms 呼ばないと計測が Inactive に落ちる）.
 */
#ifndef KIRIN_HYPHA_FFI_H
#define KIRIN_HYPHA_FFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 不透明ハンドル. */
typedef struct KirinHypha KirinHypha;

/* RT 計測結果. 各 double の「値なし」は NaN. */
typedef struct {
  double lufs_m;        /* LUFS-M (ITU-R BS.1770-4 Momentary, 400ms) */
  double true_peak;     /* True Peak [dBTP] (400ms 実時間窓) */
  double crest;         /* Crest Factor [dB] (400ms) */
  double psr;           /* PSR = peak_dBFS - LUFS-S(3s) */
  double n_prime_total; /* Zwicker N (ISO 532-1) [sone] */
  double sharpness;     /* DIN 45692 Sharpness [acum] */
  double psb_low;       /* Perceptual Spectral Balance low  [dB] */
  double psb_mid;       /* Perceptual Spectral Balance mid  [dB] */
  double psb_high;      /* Perceptual Spectral Balance high [dB] */
  double n_prime[20];   /* 20-Bark aggregated specific loudness [sone/Bark] */
  double psb_bark[20];  /* 20-band PSB (psb) */
} KirinMeasureResult;

/* セッション集計（Phase 1 では未充填）. */
typedef struct {
  double lufs_i;        /* EBU R128 Integrated [LUFS] */
  double lra;           /* EBU R128 Loudness Range [LU] */
  double max_true_peak; /* セッション内 True Peak 最大 [dBTP] */
} KirinSessionSummary;

/* ランタイム生成. sample_rate!=48000 は内部で 48k 変換. num_channels は stereo(2) 前提. */
KirinHypha* kirin_hypha_create(uint32_t sample_rate, uint32_t num_channels);

/* 信号状態（0=Inactive 1=Active 2=Bypassed）. */
void kirin_hypha_set_signal_state(KirinHypha* handle, uint8_t state);

/* interleaved f32 を供給（Audio Thread 単独・RT-safe）. num_frames==0 は keepalive 可. */
void kirin_hypha_push_samples(KirinHypha* handle, const float* interleaved,
                              size_t num_frames, uint32_t num_channels);

/* 最新 RT 計測結果を out へ. 値あり=true / 未計測・競合=false（UI Thread）. */
bool kirin_hypha_poll_result(KirinHypha* handle, KirinMeasureResult* out);

/* セッション集計を out へ. Phase 1 では常に false（symbol のみ）. */
bool kirin_hypha_poll_session(KirinHypha* handle, KirinSessionSummary* out);

/* 破棄（shutdown -> Measure Thread join）. */
void kirin_hypha_destroy(KirinHypha* handle);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* KIRIN_HYPHA_FFI_H */
