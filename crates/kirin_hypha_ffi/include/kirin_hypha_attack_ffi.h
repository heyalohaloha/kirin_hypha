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
#define KIRIN_ATTACK_BAND_BATCH_CAPACITY 240u
#define KIRIN_ATTACK_BAND_HEAD_POINTS 96u
#define KIRIN_ATTACK_BAND_TAIL_POINTS 64u
/* DRUM帯域の片側の結果。PENDINGは未計測、NOT_KEPTは音を保持していない（帯域を選ぶ前、または
 * 再生が止まってピークまで届かなかった）、ABSENTはこの打音にその側がない（PREのみ等）。 */
#define KIRIN_ATTACK_BAND_SIDE_PENDING 0u
#define KIRIN_ATTACK_BAND_SIDE_RISES 1u
#define KIRIN_ATTACK_BAND_SIDE_RINGS_ON 2u
#define KIRIN_ATTACK_BAND_SIDE_SILENT 3u
#define KIRIN_ATTACK_BAND_SIDE_NOT_KEPT 4u
#define KIRIN_ATTACK_BAND_SIDE_ABSENT 5u
#define KIRIN_ATTACK_BAND_ARRIVAL_AT 0u
#define KIRIN_ATTACK_BAND_ARRIVAL_RINGING 1u
#define KIRIN_ATTACK_BAND_RELEASE_AT 0u
#define KIRIN_ATTACK_BAND_RELEASE_NEXT_HIT 1u
#define KIRIN_ATTACK_BAND_RELEASE_AT_LEAST 2u
#define KIRIN_ATTACK_BAND_PRE_OFF 0u
#define KIRIN_ATTACK_BAND_PRE_SAME 1u
#define KIRIN_ATTACK_BAND_PRE_WAITING 2u
#define KIRIN_ATTACK_BAND_PRE_PREDATES 3u
#define KIRIN_ATTACK_BAND_KIND_POST_ALONE 4u
#define KIRIN_ATTACK_BAND_PRESENCE_FLOOR_DBFS (-72.0f)

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

/* DRUM帯域（B-1096, B-1098）: 1打の片側。時刻はmeasured_at_sampleからのms。stateがRISESのときだけ
 * 全値に意味があり（arrival_state・release_stateが限定）、RINGS_ONとSILENTはlevel_dbfsとpeak_msだけ。 */
typedef struct {
  uint8_t state;         /* KIRIN_ATTACK_BAND_SIDE_* */
  uint8_t arrival_state; /* KIRIN_ATTACK_BAND_ARRIVAL_*: RINGINGは前の打音の余韻で始まりを測れない */
  uint8_t release_state; /* KIRIN_ATTACK_BAND_RELEASE_*: AT_LEASTは測った尾の終わりでも-20 dBに届かない */
  uint8_t reserved;
  float peak_ms;
  float arrival_ms;  /* 帯域がピーク-20 dBを上向きに越える時刻 */
  float attack_ms;   /* ピークの10 %から90 %まで */
  float release_ms;  /* ピークからピーク-20 dBまで。AT_LEASTのときはその下限 */
  float level_dbfs;  /* 帯域包絡のピーク */
} KirinAttackBandSide;

/* 打音はレーンと同じ: pairが有効ならpair event、そうでなければPOST自身のdetail。event_sampleが同じ鍵。
 * kindはpair eventの0..3、pairがないときKIRIN_ATTACK_BAND_KIND_POST_ALONE。 */
typedef struct {
  int64_t event_sample;
  int64_t measured_at_sample; /* 両側を測った位置。matchedではPREのonset */
  uint8_t kind;
  uint8_t reserved[7];
  KirinAttackBandSide pre;
  KirinAttackBandSide post;
} KirinAttackBandHit;

/* statusはpair viewと同じ語彙。bandは打音の帯域（0=なし）。pre_bandはKIRIN_ATTACK_BAND_PRE_*。 */
typedef struct {
  uint8_t status;
  uint8_t band;
  uint8_t pre_band;
  uint8_t reserved;
  uint32_t count;
  uint32_t capacity;
  uint32_t resolution_micros; /* 帯域の時間の細かさ: 中心周波数の1周期 */
  uint64_t generation;
  uint32_t sample_rate;
  uint32_t reserved2;
  KirinAttackBandHit hits[KIRIN_ATTACK_BAND_BATCH_CAPACITY];
} KirinAttackBandBatch;

/* [measured_at-20 ms, +40 ms) と [measured_at, +300 ms) のdBFS。 */
typedef struct {
  float head_dbfs[KIRIN_ATTACK_BAND_HEAD_POINTS];
  float tail_dbfs[KIRIN_ATTACK_BAND_TAIL_POINTS];
} KirinAttackBandEnvelope;

/* 1打と両側の包絡。hitはbatchと同じ記録、bandはその帯域。包絡はその側がRISES・RINGS_ON・SILENTのとき
 * だけ意味を持つ。 */
typedef struct {
  KirinAttackBandHit hit;
  uint8_t band;
  uint8_t reserved[7];
  KirinAttackBandEnvelope pre;
  KirinAttackBandEnvelope post;
} KirinAttackBandHitEnvelope;

/* DRUM帯域のまとめ（2026-09-29）: その帯域で音が立ち上がった直近の打音（最大8打）をまとめたもの。
 * POST-PREではmatchedの両側が立ち上がった打音、それ以外はPOSTが立ち上がった打音を使う。
 * 余韻だけ・無音・未保持の打音はleft_outに数え、測定中の打音は数えない。 */
#define KIRIN_ATTACK_BAND_SUMMARY_HITS 8
#define KIRIN_ATTACK_BAND_LANE_NONE 0   /* 値を持つ打音がない（PREのないDELAYなど） */
#define KIRIN_ATTACK_BAND_LANE_VALUE 1  /* 中央値が差（またはPOST自身の値）を示す */
#define KIRIN_ATTACK_BAND_LANE_WITHIN 2 /* 中央値がwithin以内: 帯域で見分けられない差、またはPOSTのATTの上限 */
#define KIRIN_ATTACK_BAND_LEVEL_WITHIN_DB (0.2f)
/* 値のない打音がなぜ値を持たないか（その中で最も多い理由）: なし、前の余韻で立ち上がりが
 * 見えない、次の打音で減衰が切れた、減衰が窓を越えた。 */
#define KIRIN_ATTACK_BAND_HELD_NONE 0
#define KIRIN_ATTACK_BAND_HELD_RINGING 1
#define KIRIN_ATTACK_BAND_HELD_NEXT_HIT 2
#define KIRIN_ATTACK_BAND_HELD_LONG_TAIL 3

typedef struct {
  uint8_t state; /* KIRIN_ATTACK_BAND_LANE_* */
  uint8_t count; /* この段に値を持つ打音の数 */
  uint8_t agree; /* そのうち中央値と同じ向きの打音の数（POST-PREで差があるときだけ） */
  uint8_t withheld; /* KIRIN_ATTACK_BAND_HELD_* */
  float median;
  float low;
  float high;
  float within;
  float values[KIRIN_ATTACK_BAND_SUMMARY_HITS]; /* 各打音の値。古い順、値がなければNaN */
} KirinAttackBandLaneSummary;

typedef struct {
  uint8_t status;   /* pair viewと同じ語彙 */
  uint8_t band;
  uint8_t pre_band; /* KIRIN_ATTACK_BAND_PRE_* */
  uint8_t delta;    /* 1: POST-PRE、0: POST自身の値 */
  uint32_t count;
  uint32_t left_out;
  uint32_t resolution_micros;
  uint64_t generation;
  uint32_t sample_rate;
  uint32_t reserved;
  int64_t event_samples[KIRIN_ATTACK_BAND_SUMMARY_HITS]; /* まとめた打音のレーンの鍵。古い順 */
  KirinAttackBandLaneSummary lanes[4];                   /* DELAY, ATT, REL, LEVEL */
  float pre_arrival_ms;      /* 以下4つは中央値。onsetからのms、なければNaN */
  float post_arrival_ms;
  float pre_release_end_ms;
  float post_release_end_ms;
  KirinAttackBandEnvelope pre;       /* 平均の包絡（PREはPOST-PREのときだけ） */
  KirinAttackBandEnvelope post;
  KirinAttackBandEnvelope post_low;  /* POSTの各点の最小と最大 */
  KirinAttackBandEnvelope post_high;
} KirinAttackBandSummary;

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
bool kirin_hypha_poll_attack_band_summary(KirinHypha* handle, KirinAttackBandSummary* out);
bool kirin_hypha_poll_attack_band_envelope(KirinHypha* handle, int64_t event_sample,
                                           KirinAttackBandHitEnvelope* out);

#endif /* KIRIN_HYPHA_ATTACK_FFI_H */
