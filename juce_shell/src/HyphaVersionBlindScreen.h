#pragma once

#include "HyphaBlindScreen.h"
#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
// REF の VERSION BLIND を PRE/POST Blind（LIVE BLIND）と同じ画面で出す中身（2026-10-04 Daisuke が「PRE/POST Blind と
// 同じ画面」を選んだ）。エディターが窓全体に出し、Blind の開始・比較中・開示・中断のあいだ REF の画面を覆う。
// 指示の文は LIVE BLIND と同じ（再生したまま 1 と 2 を切り替える → 聴き比べたら開示できる → 開示した）。開示の後は
// ボタンが「1: A」「2: V」（Kirin OS の 1 曲だけの B では「2: B」）になり、そのまま切り替えられる。図は終了の後に
// V の画面で見る（Blind の画面には出さない）。A を下げて始めた Blind は、終了で戻る量を説明の行が言う。
blind_ui::Screen versionBlindScreen (const State&);
}
