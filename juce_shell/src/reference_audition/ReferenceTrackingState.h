#pragma once

namespace hypha::reference_audition
{
// H3／H4：音量合わせの動き。B・V は A の直近の窓に追従し、C は Match の後に固定する。
// stoppedCeiling は追従が上限（True Peak）に当たって止まり、直前の gain を保っている状態。
// stoppedRange は追従が MATCH の gain から ±6 dB を超えようとして止まり、直前の gain を保っている状態
// （live PRE/POST 比較の AUTO、INV-LC16 と同じ幅）。
// 画面（H9 の状態の帯）も同じ値を読むので、制御の見出しから分けて置く。
enum class TrackingState { none, following, fixed, stoppedCeiling, stoppedRange };
}
