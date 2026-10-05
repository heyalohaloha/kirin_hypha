#pragma once

#include "HyphaReferenceGuide.h"
#include "HyphaReferenceStatusModel.h"

// 2026-10-06：段階の表（INV-S48）。B・C・V のどの役も、今の段階の種類・状態の行の文（理由 / 直し方）・案内の文・待ちの
// 上限と超えたときの文を、この表だけから引く（役ごとの分岐で作ると、ある役だけ決まりから外れた：Kirin OS を待つ B の
// ページがいつも「できない」と言っていた）。待ちの上限は自動で進む段階にだけ置き、利用者の操作を待つ段階には置かない。
namespace hypha::reference_ui
{
enum class WaitBudget
{
    none,             // 利用者の操作を待つ（上限なし）
    kirinOsResponse,  // Kirin OS の応答（5 秒）
    preparation,      // 音源の確認・読み込み・準備（10 秒）
    alignmentPlay,    // 位置合わせ（再生 30 秒ぶん）
};

struct Stage
{
    StatusKind kind = StatusKind::unable;
    WaitBudget budget = WaitBudget::none;
    const char* status = "";   // 状態の行：「理由 / 直し方」（%1 は役の文字）。聴けるは「READY / A REMAINS LIVE」
    const char* overdue = "";  // 上限を超えたときの「理由 / 直し方」（上限が無ければ空）
    const char* brief = "";    // 案内の行の短い文、まだ鳴らせない役を押したときの理由
    const char* heading = "";  // 案内の見出し（%1 は役の文字）
    const char* detail = "";   // 案内の説明
};

const Stage& stageOf (SourceStep) noexcept;
// 状態の行の文（%1 を役の文字にした「理由 / 直し方」）と、案内の見出し。
juce::String stageLine (SourceStep, int slot);
juce::String stageHeading (SourceStep, int slot);
double budgetSeconds (WaitBudget) noexcept;
// 自動で進む段階（待ちの上限がある段階）。
inline bool automaticStage (SourceStep step) noexcept { return stageOf (step).budget != WaitBudget::none; }
inline constexpr int stageCount = static_cast<int> (SourceStep::attention) + 1;
}
