#ifndef KIRIN_HYPHA_METER_HISTORY_FFI_H
#define KIRIN_HYPHA_METER_HISTORY_FFI_H

#include <stdint.h>

#include "kirin_hypha_channels.h"

/* TIME 履歴の C ABI.
 *
 * B-962 で clip_event_count を [2] から [KIRIN_MAX_CHANNELS] へ広げ、measurement_epoch を
 * **先頭**へ置いた。先頭にするのは、ゼロ初期化された構造体が「区間 0 の実測」に見えないように
 * するためではなく、区間が値の身元であることを並びで示すためである。
 * 版の照合は kirin_hypha_abi_contract が行う。 */

typedef struct {
  double min;
  double max;
  double mean;
} KirinMeterHistoryRange;

/* TIME履歴1点。10 Hzはexact（min=max=mean）、低rate層は100 ms事実の集約。 */
typedef struct {
  /* どの測定区間の点か。generation も run_id も session ごとに 1 から数え直すので、
   * 別 layout / rate で作り直した engine の最初の行は前の区間の最初の行と同じ番号になる。
   * 区間をまたいだ継続かどうかを言えるのはこの値だけである。 */
  uint64_t measurement_epoch;
  uint64_t generation;
  uint64_t run_id;
  uint64_t first_observed_frames;
  uint64_t last_observed_frames;
  int64_t first_timeline_endpoint_samples; /* 不明はINT64_MIN */
  int64_t last_timeline_endpoint_samples;  /* 不明はINT64_MIN */
  uint16_t observation_count;
  uint8_t resolution; /* KIRIN_METER_HISTORY_* */
  uint8_t reserved;
  /* この履歴点の区間内で新たに始まったsample clip run数。入力チャンネルごと。
   * KirinMeterSession.channels 以降のスロットは測定を持たない。 */
  uint32_t clip_event_count[KIRIN_MAX_CHANNELS];
  KirinMeterHistoryRange lufs_m;
  KirinMeterHistoryRange lufs_s;
  KirinMeterHistoryRange true_peak;
  KirinMeterHistoryRange correlation;
  KirinMeterHistoryRange plr;
} KirinMeterHistoryEntry;

#endif
