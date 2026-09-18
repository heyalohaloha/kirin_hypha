#ifndef KIRIN_HYPHA_CHANNELS_H
#define KIRIN_HYPHA_CHANNELS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 不透明ハンドル. */
typedef struct KirinHypha KirinHypha;

/* C ABI が運ぶチャンネルスロット数. **容量であって対応宣言ではない**（D-3）.
 * 認識する最大配置は 7.1.4 = 12ch. 余りは、配置が増えたときに ABI を作り直さずに済むためにある.
 * `channel_count` 以降のスロットは測定を持たない. */
#define KIRIN_MAX_CHANNELS 16u

/* スロットに役割が無いことを表す値. どの KirinChannelRole とも重ならない. */
#define KIRIN_CHANNEL_ROLE_NONE 255u

/* 認識済みレイアウトの識別子. 0 はレイアウト不明で、**レイアウトではない**.
 * ゼロ初期化された構造体はここが 0 になるので、未初期化を mono と取り違えない.
 * 値は crates/kirin_measure/src/channel_layout.rs の `LayoutId` discriminant と一致させること. */
typedef enum {
  KIRIN_LAYOUT_ID_UNKNOWN = 0,
  KIRIN_LAYOUT_ID_MONO = 1,
  KIRIN_LAYOUT_ID_STEREO = 2,
  KIRIN_LAYOUT_ID_5_0 = 3,
  KIRIN_LAYOUT_ID_5_1 = 4,
  KIRIN_LAYOUT_ID_7_1_4 = 5
} KirinChannelLayoutId;

/* チャンネル 1 本の役割（ITU-R BS.2051 の位置名）.
 *
 * 値は crates/kirin_measure/src/channel_layout.rs の `ChannelRole` discriminant と一致させること.
 * 既存の値は決して振り直さない（古い殻と新しい staticlib が組み合わされたとき、
 * ずれたコードは「それらしい」別チャンネルとして無言で計測される）. */
typedef enum {
  KIRIN_CHANNEL_ROLE_CENTRE = 0,             /* M+000 */
  KIRIN_CHANNEL_ROLE_LEFT = 1,               /* M+030 */
  KIRIN_CHANNEL_ROLE_RIGHT = 2,              /* M-030 */
  KIRIN_CHANNEL_ROLE_LFE = 3,                /* LFE. loudness からは除外し peak / clip は観測する */
  KIRIN_CHANNEL_ROLE_LEFT_SURROUND = 4,      /* M+110 */
  KIRIN_CHANNEL_ROLE_RIGHT_SURROUND = 5,     /* M-110 */
  KIRIN_CHANNEL_ROLE_LEFT_SURROUND_SIDE = 6, /* M+090 */
  KIRIN_CHANNEL_ROLE_RIGHT_SURROUND_SIDE = 7,/* M-090 */
  KIRIN_CHANNEL_ROLE_LEFT_SURROUND_REAR = 8, /* M+135 */
  KIRIN_CHANNEL_ROLE_RIGHT_SURROUND_REAR = 9,/* M-135 */
  KIRIN_CHANNEL_ROLE_TOP_FRONT_LEFT = 10,    /* U+045 */
  KIRIN_CHANNEL_ROLE_TOP_FRONT_RIGHT = 11,   /* U-045 */
  KIRIN_CHANNEL_ROLE_TOP_REAR_LEFT = 12,     /* U+135 */
  KIRIN_CHANNEL_ROLE_TOP_REAR_RIGHT = 13     /* U-135 */
} KirinChannelRole;

/* ランタイム生成. sample_rate != 48000 は内部で 48k 変換.
 *
 * channel_roles は KirinChannelRole の値を **バッファ順**に並べた配列, channel_count はその長さ.
 * チャンネル数ではなく役割を渡すのは、数がレイアウトを決めないため（8ch は 7.1 でも 5.1.2 でもある）.
 *
 * 認識できない役割・重複・順序違い・未知のレイアウト・null・0 は **null を返して拒否する**.
 * 「それらしい」map で計測を続けない. 呼び出し側は null ハンドルを許容すること.
 *
 * 現在の受理範囲は mono（CENTRE）と stereo（LEFT, RIGHT）のみ. サラウンドは engine 側の
 * Nch 化が済むまで拒否する（殻は isBusesLayoutSupported でそもそも交渉しない）. */
KirinHypha* kirin_hypha_create(uint32_t sample_rate, const uint8_t* channel_roles,
                               uint32_t channel_count);

#ifdef __cplusplus
}
#endif

#endif
