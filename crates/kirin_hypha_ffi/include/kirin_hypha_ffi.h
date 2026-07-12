/*
 * kirin_hypha_ffi.h — Kirin Hypha JUCE 移植の C ABI（正本・手書き）.
 *
 * 対応 staticlib:
 *   - Windows/MSVC: target/{debug,release}/kirin_hypha_ffi.lib
 *   - macOS/Linux:  target/{debug,release}/libkirin_hypha_ffi.a
 * 実装:           crates/kirin_hypha_ffi/src/lib.rs（このヘッダと常に一致させること）.
 *
 * C ABI surface（すべて実装済み）:
 *   - RT 計測: create / set_signal_state / push_samples / poll_result / destroy.
 *   - Record:  set_license / exit_record / poll_session
 *              （SessionSummary は Record finalize 後に成立。finalize は Measure Thread のみ）.
 *   - 識別子:  set_identity / get_identity（state chunk 方式A）.
 *   - IO:      enable_pre_writes（PRE）/ enable_post_writes（POST）.
 *   - ペアリング: set_pair_target / set_pre_name / keep / stop / poll_delta（POST Keep → PRE ack）.
 *   - 一斉操作: keep_all / stop_all（broadcast + self）/ enumerate_pre_candidates /
 *              enumerate_post_pair_claims（B-102 / 新↔新）.
 *   - LED poller: measure_alive / record_acknowledged / preset_available（B-054 / read-only）.
 *   - Note:    add_annotation.
 *   - Option<f64> は NaN sentinel で表す（C 側は isnan() で「値なし」を判定）.
 *
 * スレッド契約:
 *   - push_samples: Audio Thread 単独・RT-safe（内部は rtrb push + heartbeat++ のみ）.
 *   - poll_result : UI Thread（内部は try_lock 非ブロッキング）.
 *   - push_samples は毎オーディオブロック呼ぶこと（B-118/G-115-245: ~3s 呼ばないと計測が Inactive に落ちる）.
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
  double true_peak;     /* tp_recent: True Peak 直近 400ms (frame基準, LUFS-M と同窓 / B-074) [dBTP] */
  double crest;         /* Crest Factor [dB] (400ms) */
  double psr;           /* PSR = peak_dBFS - LUFS-S(3s) */
  double n_prime_total; /* Zwicker N (ISO 532-1) [sone] */
  double sharpness;     /* DIN 45692 Sharpness [acum] */
  double psb_low;       /* Perceptual Spectral Balance low  [dB] */
  double psb_mid;       /* Perceptual Spectral Balance mid  [dB] */
  double psb_high;      /* Perceptual Spectral Balance high [dB] */
  double n_prime[20];   /* 20-Bark aggregated specific loudness [sone/Bark] */
  double psb_bark[20];  /* 20-band PSB (psb) */
  double tp_session_max;/* tp_session_max: init 以降の inter-sample running max [dBTP]
                           (Record 正本と同定義 / B-074: 末尾追加で既存 offset 不変) */
  uint64_t dropped_samples;/* ring 満杯で測定 ring に push できなかった累積サンプル数
                              (>0 = integrity 低下 / B-075: 計測値は不変・欠落の露出のみ) */
} KirinMeasureResult;

typedef struct {
  KirinMeasureResult current;
  KirinMeasureResult maximum;
} KirinWatchDisplay;

/* セッション集計（Record finalize 後に充填・Record 前は未充填）. */
typedef struct {
  double lufs_i;        /* EBU R128 Integrated [LUFS] */
  double lra;           /* EBU R128 Loudness Range [LU] */
  double max_true_peak; /* セッション内 True Peak 最大 [dBTP] */
} KirinSessionSummary;

/* state chunk 往復する識別子（方式A）. 各フィールドは null 終端 C 文字列（最大 63 + null）.
 * project_hash は派生値のため含めない（JUCE は下記 4 キーを chunk に保存する）. */
typedef struct {
  char instance_id[64];
  char project_uuid[64];
  char daw_session_uuid[64];
  char name[64];
} KirinIdentity;

/* POST の Δ（3d-b）. 各 double の「値なし」は NaN. mode: 0=Active 1=Stale 2=NoPre 3=Bypassed. */
typedef struct {
  uint8_t mode;
  double lufs;
  double true_peak;
  double crest;
  double psr;
  double n_prime_total;
  double sharpness;
} KirinDelta;

/* pair 候補 1 件（B-102 / GUI ドロップダウン用）. instance_id は null 終端（最大 63 文字）.
 * name は PRE 表示名（has_name=0 のとき内容不定）. enumerate_pre_candidates が固定長配列へ書く. */
typedef struct {
  char instance_id[64];
  char name[64];
  uint8_t has_name;
} KirinPreCandidate;

/* POST pair claim 1 件（GUI ドロップダウンの keepability 表示用）.
 * pair_pre_name は POST が選択中の PRE 名（has_pair_pre_name=0 のとき内容不定）. */
typedef struct {
  char instance_id[64];
  char pair_pre_name[64];
  uint8_t has_pair_pre_name;
} KirinPostPairClaim;

/* ランタイム生成. sample_rate!=48000 は内部で 48k 変換. num_channels は 1=mono / 2=stereo. */
KirinHypha* kirin_hypha_create(uint32_t sample_rate, uint32_t num_channels);

/* 信号状態（0=Inactive 1=Active 2=Bypassed）. */
void kirin_hypha_set_signal_state(KirinHypha* handle, uint8_t state);

/* 現在の信号状態を読む（0=Inactive 1=Active 2=Bypassed）. LED poller 系（read-only）.
 * Measure Thread の heartbeat 停止検出で Inactive へ上書きされた値も反映する（B-113）. */
uint8_t kirin_hypha_get_signal_state(KirinHypha* handle);

/* identity.json からライセンスコードを読む（0=Os 1=Sense 2=Unknown）. ハンドル不要.
 * ~/Library/Application Support/Kirin OS/identity.json の "license" を loose 抽出.
 * ファイル不在・parse 失敗・$HOME 不在は 2=Unknown（安全側）. 殻は戻り値を set_license に渡す. */
uint8_t kirin_hypha_load_license(void);

/* ライセンス設定（0=Os 1=Sense 2=Unknown / 未知値は安全側 Unknown）.
 * identity.rs:46 の enum 宣言順に一致. Os 以外へ降格時 Record 中なら強制 Watch（E-21）. */
void kirin_hypha_set_license(KirinHypha* handle, uint8_t license);

/* Record 終了（無条件 Watch・冪等）. 直近 finalize 値は session_summary に保持され
 * poll_session で取得できる（finalize は Measure Thread のみ・FFI は呼ばない）. */
void kirin_hypha_exit_record(KirinHypha* handle);

/* state chunk から復元した識別子を設定する（方式A / 3c）. enable_pre_writes の前に呼ぶ.
 * 各引数は null 終端 C 文字列（NULL 可＝空文字）. 空のキーは enable 時に生成される.
 * 復元順: create → set_license → set_identity → enable_pre_writes. */
void kirin_hypha_set_identity(KirinHypha* handle, const char* instance_id,
                              const char* project_uuid, const char* daw_session_uuid,
                              const char* name);

/* 現在の識別子を out へ（JUCE が getStateInformation で chunk へ保存する 4 キー）. */
void kirin_hypha_get_identity(KirinHypha* handle, KirinIdentity* out);

/* PRE の plugin_data 書込（Watch pre.json + Record frames/PSB）を有効化する（3b）.
 * set_license の後に 1 度呼ぶ（呼んだ時点の license をスナップショット）. 2 度目以降 no-op.
 * filesystem 書込は kirin_measure の io_thread_pre 内に閉じる（FFI は spawn のみ）. */
void kirin_hypha_enable_pre_writes(KirinHypha* handle);

/* POST のランタイム（post.json の Δ を厳格選定 select_target_pre 経由で書く）を有効化（3d-a）.
 * enable_pre_writes と対・同一 engine では排他（片方のみ・冪等）. pair_pre_name は
 * 既定 identity.name（set_pair_target で上書き可）. */
void kirin_hypha_enable_post_writes(KirinHypha* handle);

/* POST の対 PRE 名（pair target）を設定（3d-b / identity.name 結合を解く）. NULL=空文字.
 * 値は内部で sanitize される（ASCII graphic + space / 最大 16 文字）. PRE 名と同一語彙.
 * enable_post_writes 後でも live 反映（io_thread と Arc 共有）. */
void kirin_hypha_set_pair_target(KirinHypha* handle, const char* name);

/* PRE の自名（pre name）を設定（B-054 / set_pair_target と完全対称）. NULL=空文字.
 * 値は内部で sanitize される（ASCII graphic + space / 最大 16 文字）. POST 側 pair target と
 * 同一語彙. enable_pre_writes 後でも live 反映（io_thread と Arc 共有）. 空文字はそのまま空 name
 * として pre.json に書かれる（instance_id 先頭8字 fallback は GUI 表示専用）. */
void kirin_hypha_set_pre_name(KirinHypha* handle, const char* name);

/* Measure Thread が生存しているか（B-054 LED Error 状態 / read-only poller・UI Thread）.
 * B-118: FFI も T-8 watchdog を spawn する（B-056 opt-out 撤回）. 本値は watchdog が Measure crash
 * 検出で false / 再 spawn 復帰で true を書くフラグ. null は false. */
bool kirin_hypha_measure_alive(KirinHypha* handle);

/* B-115: heartbeat 鮮度（processBlock が呼ばれている事実 / read-only poller・UI Thread）.
 * signal_state とは別軸（無音再生中も state=Inactive のため state を live の代用にしない）.
 * 殻は「playing かつ live」で POST pair 変更をロックする. null / panic は false. */
bool kirin_hypha_heartbeat_live(KirinHypha* handle);

/* B-118 Phase 3 (②): 現プロジェクトが Record 排他上限（12）に達しているか（advisory / UI Thread）.
 * keep 経路は reserve→count>MAX を正本にするため、同一 pairing の再Keepを 12 枠ちょうどで
 * pre-reject しない. null / panic は false. */
bool kirin_hypha_record_exclusion_conflict(KirinHypha* handle);

/* B-118 Phase 3 (③): io_thread 連続失敗の固定文言（G-115-29）を out（最大 out_len-1 + null 終端）へ書く.
 * 文言あり=true / 通常(None)・null・panic=false（out 不変）. read-only poller・UI Thread. */
bool kirin_hypha_record_error_message(KirinHypha* handle, char* out, size_t out_len);

/* B-128 (G-115-373 / D3): restore identity の anomaly を当該 instance の分だけ out（最大 len-1 + null
 * 終端）へ 1 件 drain する（per-instance routing）. handle の instance_id に tag された materialize event か
 * instance context のない wall event（global）を返す（他 instance の event は返さない）. event あり=true /
 * 無し・null out・panic=false. handle=null は global event のみ. 殻は毎描画 / timer で poll して surface. */
bool kirin_hypha_drain_path_event(KirinHypha* handle, char* out, size_t len);

/* PRE が POST の record_signal を ack 済みか（B-054 LED / Keeping バナー poller・UI Thread）.
 * enable_pre_writes 前 / POST engine / null は false. */
bool kirin_hypha_record_acknowledged(KirinHypha* handle);

/* POST に pair 可能な PRE preset（record_signal）が居るか（B-054 PresetAvailable LED poller・
 * UI Thread）. enable_post_writes 前 / PRE engine / null は false. */
bool kirin_hypha_preset_available(KirinHypha* handle);

/* POST「Keep」: 厳格選定で対 PRE を一意決定し record_signal(pending) を書く（3d-b）.
 * PRE 側 io_thread が autonomous に discover→ack する. Os かつ一意 PRE のとき true /
 * 選定 None（空名/不在/曖昧/Inactive/古t）・非 Os・既 Record(no-op) は false. */
bool kirin_hypha_keep(KirinHypha* handle);

/* Drop 後の WAV expected metadata を登録し、閉じた今回 Record を即時再照合する
 * （Kirin OS/JUCE runtime 入口）. Keep は事前登録を必要とせず、前回世代を結ばない.
 * bounce_id / wav_hash / wav_path は null 終端 C 文字列（NULL 不可・空不可）. */
bool kirin_hypha_set_expected_wav_metadata(KirinHypha* handle, const char* bounce_id,
                                           uint64_t expected_duration_samples,
                                           uint32_t expected_sample_rate,
                                           const char* wav_path, uint64_t wav_file_size,
                                           int64_t wav_mtime_ms, const char* wav_hash);

/* POST が Record 中か（true=Record / false=Watch）. pairing UI の Keep/Stop 出し分け用. */
bool kirin_hypha_is_recording(KirinHypha* handle);

/* Record take の実レンダー長を通知する（Audio Thread 単独・RT-safe）.
 * rendered はこの block をDAWが実処理したことを示す. position_* は host の
 * sample position が取れる場合だけ valid=true. */
void kirin_hypha_note_record_block(KirinHypha* handle, bool recording, bool rendered,
                                   bool playing, bool offline, bool position_valid,
                                   int64_t position_samples, uint64_t num_frames);

/* Record take のWAV/native clock windowを通知する（Audio Thread単独・RT-safe）.
 * clock_end_valid=true の時だけ clock_start/end を duration_samples の正本として使う.
 * 旧 note_record_block は clock end 不明の render-span fallback として残す. */
void kirin_hypha_note_record_window(KirinHypha* handle, bool recording, bool rendered,
                                    bool playing, bool offline, bool position_valid,
                                    int64_t position_samples, uint64_t num_frames,
                                    int64_t clock_start_samples, bool clock_end_valid,
                                    int64_t clock_end_samples);

/* measurement ring へ実際に投入する窓の host sample clock（Audio Thread単独・RT-safe）.
 * Watch pre-roll / Record 共通。対応する push_samples の直前に1回だけ呼ぶ. */
#define KIRIN_HYPHA_CLOCK_UNKNOWN 0
#define KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE 1
#define KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE 2
#define KIRIN_HYPHA_PRESENTATION_SOURCE_UNKNOWN 0
#define KIRIN_HYPHA_PRESENTATION_SOURCE_VST3 1
#define KIRIN_HYPHA_PRESENTATION_SOURCE_AUDIO_UNIT_V2 2
void kirin_hypha_note_capture_window(KirinHypha* handle, bool position_valid,
                                     int64_t position_samples, uint64_t num_frames,
                                     uint8_t clock_source,
                                     uint8_t presentation_source,
                                     bool input_presentation_valid,
                                     uint32_t input_presentation_samples,
                                     bool output_presentation_valid,
                                     uint32_t output_presentation_samples);

/* Watch MAX pass boundary notification (Audio Thread, RT-safe). */
void kirin_hypha_note_transport_block(KirinHypha* handle, bool playing,
                                      bool position_valid, int64_t position_samples,
                                      uint64_t num_frames);

/* Current Watch values + current playback-pass MAX (UI thread). */
bool kirin_hypha_poll_watch_display(KirinHypha* handle, bool playing,
                                    KirinWatchDisplay* out);

/* POST「Stop」: pair 解除（record_signal released）+ Watch へ戻す（3d-b）. */
void kirin_hypha_stop(KirinHypha* handle);

/* POST「All Keep」: all_keep broadcast を書いてから自身の keep を発火（B-102 / 新↔新）.
 * 同一 DAW セッションの他 POST が各自の pair を keep する. 自 keep 成功（有効ペアあり）で true. */
bool kirin_hypha_keep_all(KirinHypha* handle);

/* POST「All Stop」: all_stop broadcast を書いてから自身の stop を発火（B-102 / 新↔新）. */
void kirin_hypha_stop_all(KirinHypha* handle);

/* pair 候補（Active な PRE）を out（最大 cap 件）へ書き、書いた件数を返す（B-102 / UI Thread）.
 * out は呼び出し側確保の KirinPreCandidate[cap]. cap 超過は切り捨て. null / cap==0 は 0. */
size_t kirin_hypha_enumerate_pre_candidates(KirinHypha* handle, KirinPreCandidate* out, size_t cap);

/* All Keep の「N ready」= pair 設定済の Active POST 数（B-102 / egui n_ready と同一・UI Thread）.
 * null は 0. */
size_t kirin_hypha_count_keep_ready(KirinHypha* handle);

/* POST 側の pair claim を out（最大 cap 件）へ書き、書いた件数を返す（UI Thread）.
 * GUI は PRE候補名と照合して "Can Keep" / "Keep ready" / "In use" を表示する. */
size_t kirin_hypha_enumerate_post_pair_claims(KirinHypha* handle, KirinPostPairClaim* out, size_t cap);

/* POST の Δ を out へ（3d-b / GUI 表示用）. 値あり=true / 競合・未計測=false（UI Thread）.
 * post.json には Δ でなく POST 生メトリクスが入る（Δ はこの API で公開）. */
bool kirin_hypha_poll_delta(KirinHypha* handle, KirinDelta* out);

/* Record の最新 plugin_data .json に利用者メモを追記する（Note / 方式A）.
 * 書込先はこの engine の役割（enable_pre_writes=PRE / enable_post_writes=POST）の .json.
 * Os かつ enable 済（role 確定）かつ対象 .json 存在のとき true / それ以外（未 enable 含む）false. */
bool kirin_hypha_add_annotation(KirinHypha* handle, const char* memo);

/* interleaved f32 を供給（Audio Thread 単独・RT-safe）. num_frames==0 は keepalive 可. */
void kirin_hypha_push_samples(KirinHypha* handle, const float* interleaved,
                              size_t num_frames, uint32_t num_channels);

/* B-125: prealloc-max 超の病的 block を drop した interleaved sample 数を計上（Audio Thread 単独）.
 * dropped_samples = num_frames * num_channels. 本体は fetch_add のみ（alloc/lock/syscall なし）.
 * push_overflow とは別カウンタ（混ぜない）が dropped_samples / integrity_degraded には合算反映. */
void kirin_hypha_note_oversized_drop(KirinHypha* handle, uint64_t dropped_samples);

/* 最新 RT 計測結果を out へ. 値あり=true / 未計測・競合=false（UI Thread）. */
bool kirin_hypha_poll_result(KirinHypha* handle, KirinMeasureResult* out);

/* セッション集計を out へ. Record finalize 後に値あり=true / 未 Record・競合=false（UI Thread）.
 * ABI signature は Phase 1 と不変（Record 前は false のまま）. */
bool kirin_hypha_poll_session(KirinHypha* handle, KirinSessionSummary* out);

/* 破棄（shutdown -> Measure Thread join）. */
void kirin_hypha_destroy(KirinHypha* handle);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* KIRIN_HYPHA_FFI_H */
