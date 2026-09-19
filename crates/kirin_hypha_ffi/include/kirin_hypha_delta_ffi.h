/* Δ（POST − PRE）の mode コード。
 *
 * 殻は `== KIRIN_DELTA_MODE_ACTIVE` で表示を決める。それ以外は Δ を出さない。
 * B-976 で配置に関する 2 コードを足したときに kirin_hypha_ffi.h から分けた（行数規律）。 */
#ifndef KIRIN_HYPHA_DELTA_FFI_H
#define KIRIN_HYPHA_DELTA_FFI_H

#define KIRIN_DELTA_MODE_ACTIVE 0u
#define KIRIN_DELTA_MODE_STALE 1u
#define KIRIN_DELTA_MODE_NO_PRE 2u
#define KIRIN_DELTA_MODE_BYPASSED 3u
#define KIRIN_DELTA_MODE_PRE_INACTIVE 4u
/* PRE と POST が違う配置で測っている。Δ を出さない。 */
#define KIRIN_DELTA_MODE_LAYOUT_MISMATCH 5u
/* PRE が配置を名乗っていない（旧版）。compatible と確認できないので Δ を出さない。 */
#define KIRIN_DELTA_MODE_LAYOUT_UNKNOWN 6u

#endif /* KIRIN_HYPHA_DELTA_FFI_H */
