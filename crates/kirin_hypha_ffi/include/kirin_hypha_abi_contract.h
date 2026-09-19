#ifndef KIRIN_HYPHA_ABI_CONTRACT_H
#define KIRIN_HYPHA_ABI_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ヘッダとライブラリが同じ ABI で作られたかを実行時に照合する.
 *
 * `static_assert` はヘッダと **殻の** コンパイル単位しか照合しない. staticlib は先にビルド済みで、
 * 古いままの staticlib と新しいヘッダを組み合わせると、コンパイルは通り、リンクも通り、
 * offset だけが全部ずれる. そのとき出てくるのは「値が出ているのに意味が違う」状態なので、
 * 数値を出す前に弾く（D-13）.
 *
 * 殻は自分がコンパイル時に見た値（KIRIN_ABI_*）と、ライブラリが実行時に返す値を比べる.
 * 1 つでも違えば engine を作らない. */

/* ABI 全体の版. offset を動かす変更のたびに 1 つ上げる.
 * 4 = B-958（KirinMeterSession の入力チャンネル配列 [2] -> [KIRIN_MAX_CHANNELS]）.
 * 5 = B-962（KirinMeterHistoryEntry の clip_event_count 同上 + measurement_epoch）.
 * 6 = B-981（Observatory comparison state/reason/generation/identity）. */
#define KIRIN_ABI_REVISION 6u

typedef struct {
  uint32_t revision;                  /* KIRIN_ABI_REVISION */
  uint32_t observatory_frame_version; /* KIRIN_OBSERVATORY_FRAME_VERSION */
  uint32_t max_channels;              /* KIRIN_MAX_CHANNELS */
  uint32_t mono_sum_band_count;       /* KIRIN_MONO_SUM_BAND_COUNT */
  uint32_t stereo_field_bins;         /* KIRIN_STEREO_FIELD_BINS */
  uint32_t reserved;
  uint64_t meter_session_size;
  uint64_t meter_session_align;
  uint64_t observatory_frame_size;
  uint64_t measure_result_size;
  uint64_t delta_size;
  uint64_t meter_history_entry_size;
  uint64_t meter_history_entry_epoch_offset;
  /* 移動しやすい offset の実測値. 上の size が偶然一致しても、中身の配置が違えば ここで落ちる. */
  uint64_t meter_session_channels_offset;
  uint64_t meter_session_sample_peak_offset;
  uint64_t meter_session_channel_positions_offset;
  uint64_t meter_session_measurement_epoch_offset;
} KirinAbiContract;

/* このライブラリがビルドされた ABI を書き出す. out が null なら何もしない. */
void kirin_hypha_abi_contract(KirinAbiContract* out);

#ifdef __cplusplus
}
#endif

#endif
