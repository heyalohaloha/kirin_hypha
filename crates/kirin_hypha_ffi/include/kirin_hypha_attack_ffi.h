/* Kirin Hypha C ABI, ATTACK DRUM part. kirin_hypha_ffi.h includes it inside its extern "C" block
 * after KirinHypha is declared, like the other *_ffi.h parts; never include it on its own.
 * The same ABI as before the split. */
#ifndef KIRIN_HYPHA_ATTACK_FFI_H
#define KIRIN_HYPHA_ATTACK_FFI_H

#define KIRIN_ATTACK_BATCH_CAPACITY 64u
#define KIRIN_ATTACK_EVENT_BATCH_CAPACITY 240u
#define KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY 600u
#define KIRIN_ATTACK_DETAIL_BATCH_CAPACITY 240u
#define KIRIN_ATTACK_SHAPE_CAPACITY 96u
#define KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY 240u
#define KIRIN_ATTACK_BAND_BATCH_CAPACITY 64u
#define KIRIN_ATTACK_BAND_HEAD_POINTS 96u
#define KIRIN_ATTACK_BAND_TAIL_POINTS 64u

/* ATTACK DRUM内部検証用のraw SuperFlux ODF。公開Analysis route/stateには含めない。 */
typedef struct {
  uint64_t generation;
  uint32_t sample_rate;
  uint8_t channels;
  uint8_t reserved[3];
  uint8_t definition_hash[32];
  uint32_t window_samples;
  uint32_t hop_samples;
  int64_t support_start_samples;
  int64_t support_end_samples;
  int64_t event_sample;
  float value;
} KirinAttackOdfFrame;

/* UI/control threadが回収する固定長窓。framesは古い順。 */
typedef struct {
  uint32_t count;
  uint32_t capacity;
  KirinAttackOdfFrame frames[KIRIN_ATTACK_BATCH_CAPACITY];
} KirinAttackBatch;

/* B-553固定peak ruleを通過し、30 ms refractoryが確定したATTACK event。 */
typedef struct {
  uint64_t generation;
  uint32_t sample_rate;
  uint8_t channels;
  uint8_t reserved[3];
  uint8_t definition_hash[32];
  int64_t event_sample;
  int64_t decision_sample;
  float value;
} KirinAttackEvent;

typedef struct {
  uint32_t count;
  uint32_t capacity;
  KirinAttackEvent events[KIRIN_ATTACK_EVENT_BATCH_CAPACITY];
} KirinAttackEventBatch;

/* source 0基準の10 ms絶対波形envelope。stereoはmean linear power。 */
typedef struct {
  uint64_t generation;
  uint32_t sample_rate;
  uint8_t channels;
  uint8_t reserved[3];
  int64_t start_sample;
  int64_t end_sample;
  float peak_linear;
  float rms_dbfs;
} KirinAttackWaveformPoint;

typedef struct {
  uint32_t count;
  uint32_t capacity;
  KirinAttackWaveformPoint points[KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY];
} KirinAttackWaveformBatch;

/* 確定eventの事実記述子（B-1016/B-1024）。窓はonsetを含む約1 ms binから始まりPRE/POST同一。complete=0は頭30 msだけ測定済み。shapeは測定済み区間。
 * transient=頭30 ms RMS−body RMS。bodyは頭の後100 msか次onsetまで、20 ms未満ならなし。sharpnessは頭から100 msの音量加重平均。 */
typedef struct {
  uint64_t generation;
  uint32_t sample_rate;
  uint8_t channels;
  uint8_t transient_available;
  uint8_t sharpness_available;
  uint8_t complete;
  uint8_t definition_hash[32];
  int64_t event_sample;
  int64_t decision_sample;
  int64_t shape_start_sample;
  int64_t shape_end_sample;
  float value;
  float transient_db;
  float body_rms_dbfs;
  float attack_rms_dbfs;
  float sample_peak_dbfs;
  float crest_db;
  int64_t body_end_sample;
  float sharpness_acum;
  uint32_t bin_frames;
  uint32_t shape_count;
  uint32_t reserved2;
  float shape[KIRIN_ATTACK_SHAPE_CAPACITY];
} KirinAttackDetail;

typedef struct {
  uint32_t count;
  uint32_t capacity;
  KirinAttackDetail details[KIRIN_ATTACK_DETAIL_BATCH_CAPACITY];
} KirinAttackDetailBatch;

/* 同一content sample上の共通ATTACK判定。kind: 0=matched,1=PRE-only,2=POST-only,3=ambiguous. matchedのpost_event_sampleはPOSTを測ったPRE onset。 */
typedef struct {
  uint64_t pair_generation;
  uint64_t pre_generation;
  uint64_t post_generation;
  uint32_t sample_rate;
  uint8_t channels;
  uint8_t kind;
  uint8_t pre_available;
  uint8_t post_available;
  uint8_t definition_hash[32];
  int64_t event_sample;
  int64_t decision_sample;
  int64_t pre_event_sample;
  int64_t post_event_sample;
  float pre_value;
  float post_value;
  float delta_value;
  uint8_t delta_available;
  uint8_t reserved[3];
} KirinAttackPairEvent;

typedef struct {
  uint8_t status; /* KIRIN_SPECTRUM_*と同じ状態語彙 */
  uint8_t reserved[3];
  uint32_t count;
  uint32_t capacity;
  uint32_t reserved2;
  KirinAttackPairEvent events[KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY];
} KirinAttackPairEventBatch;

typedef struct {
  uint8_t available;
  uint8_t enabled;
  uint8_t worker_running;
  uint8_t channels;
  uint8_t reserved[4];
  uint64_t pushed_blocks;
  uint64_t dropped_blocks;
  uint64_t analyzed_frames;
} KirinAttackStats;

/* DRUM帯域（B-1096）: 1打の片側。時刻はonsetからのms。*_available=0の値は音に無かった事実で、0ではない。 */
typedef struct {
  uint8_t available;
  uint8_t arrival_available;
  uint8_t attack_available;
  uint8_t release_available;
  uint8_t reserved[4];
  int64_t span_end_sample; /* 測った尾の終わり（排他）: onset+300 ms か次のonset */
  float peak_ms;
  float arrival_ms;  /* 帯域がピーク-20 dBを上向きに越える時刻 */
  float attack_ms;   /* ピークの10 %から90 %まで */
  float release_ms;  /* ピークからピーク-20 dBまで */
  float level_dbfs;  /* 帯域包絡のピーク */
  float reserved2;
  float head_dbfs[KIRIN_ATTACK_BAND_HEAD_POINTS]; /* [onset-20 ms, onset+40 ms) */
  float tail_dbfs[KIRIN_ATTACK_BAND_TAIL_POINTS]; /* [onset, onset+300 ms); span_end以降は床 */
} KirinAttackBandSide;

/* kind: 0=matched（PRE onsetでPREとPOST）, 2=POSTのみ。resolution_microsは帯域の中心周波数1周期。 */
typedef struct {
  uint64_t generation;
  uint32_t sample_rate;
  uint8_t channels;
  uint8_t band; /* 1=63 Hz … 8=8 kHz */
  uint8_t kind;
  uint8_t delay_available;
  int64_t event_sample;
  uint32_t resolution_micros;
  float delay_ms; /* POST − PRE のarrival */
  KirinAttackBandSide pre;
  KirinAttackBandSide post;
} KirinAttackBandHit;

/* statusはpair viewと同じ語彙。bandは選択中の帯域（0=なし）。pre_band_availableはPREのsnapshotが
 * 同じ帯域を名乗ったときだけ1: 帯域を知らないPREは名乗らず、hitsはPOSTだけのまま。 */
typedef struct {
  uint8_t status;
  uint8_t band;
  uint8_t pre_band_available;
  uint8_t reserved;
  uint32_t count;
  uint32_t capacity;
  uint32_t reserved2;
  KirinAttackBandHit hits[KIRIN_ATTACK_BAND_BATCH_CAPACITY];
} KirinAttackBandBatch;

/* ATTACK DRUM製品導線。POSTだけが有効化可能で、画面非表示時は停止・state保存なし。 */
bool kirin_hypha_set_attack_enabled(KirinHypha* handle, bool enabled);
bool kirin_hypha_poll_attack_batch(KirinHypha* handle, KirinAttackBatch* out);
bool kirin_hypha_poll_attack_events(KirinHypha* handle, KirinAttackEventBatch* out);
bool kirin_hypha_poll_attack_waveform(KirinHypha* handle, KirinAttackWaveformBatch* out);
bool kirin_hypha_poll_attack_details(KirinHypha* handle, KirinAttackDetailBatch* out);
bool kirin_hypha_poll_attack_pre_waveform(KirinHypha* handle,
                                          KirinAttackWaveformBatch* out);
bool kirin_hypha_poll_attack_pre_details(KirinHypha* handle,
                                         KirinAttackDetailBatch* out);
bool kirin_hypha_poll_attack_pair_events(KirinHypha* handle,
                                         KirinAttackPairEventBatch* out);
bool kirin_hypha_attack_stats(KirinHypha* handle, KirinAttackStats* out);
/* DRUM帯域。POSTだけが選べ、0で解除。選んでいる間だけ計測し、state保存なし。 */
bool kirin_hypha_set_attack_band(KirinHypha* handle, uint8_t band);
bool kirin_hypha_poll_attack_band(KirinHypha* handle, KirinAttackBandBatch* out);

#endif /* KIRIN_HYPHA_ATTACK_FFI_H */
