#pragma once

namespace hypha::reference_audition
{
// 音量合わせの動き。B・V は A の直近の窓に追従し、C は Match の後に固定する。
// B・V は MATCH から ±6 dB の端でも追従を続け、範囲へ戻れば同じ MATCH から合わせる。
// 画面（状態の帯）も同じ値を読むので、制御の見出しから分けて置く。
enum class TrackingState { none, following, fixed };
}
