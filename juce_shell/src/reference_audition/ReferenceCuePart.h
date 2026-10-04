#pragma once

// 2026-10-04：Cue が曲のどの部分か。図の凡例に出す（「A直近10秒 / Bサビ」）。
// Kirin OS は Cue を決めていない曲（曲全体のまま）に、サビ候補（無ければ最も大きい 30 秒）を既定の Cue として
// 渡す。どの部分かは ranges の自動区間と照らして決める（cuePartOf、ReferenceSourceRanges.h）。
namespace hypha::reference_audition
{
enum class CuePart { unknown, whole, chorus, loudest, cue };
}
