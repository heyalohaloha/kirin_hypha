#include "HyphaReferencePreparationWatch.h"

#include "HyphaReferenceStages.h"

#include <algorithm>

namespace hypha::reference_ui
{
namespace
{
// 位置合わせ中は、合わない音で 4 秒の録りごとに「一致未確認」（noMatchingPassage）と「位置合わせ中」が入れ替わる。
// どちらも同じ待ち（合えば鳴る）なので、再生 30 秒の数えは段が入れ替わっても続け、超えたらどちらの段でも位置合わせの
// 文を出したまま待つ（2026-10-31：案内が出ず表示だけが数秒ごとに入れ替わった）。
SourceStep alignmentPhase (SourceStep step) noexcept
{
    return step == SourceStep::noMatchingPassage ? SourceStep::aligning : step;
}
}

juce::String PreparationWatch::observe (int slot, SourceStep rawStep, bool measuringA, bool kirinOsOnline, bool playing,
                                        double nowSeconds)
{
    const auto step = alignmentPhase (rawStep);
    if (slot != watchedSlot || step != watchedStep || measuringA != watchedA || kirinOsOnline != watchedOnline)
    {
        watchedSlot = slot;
        watchedStep = step;
        watchedA = measuringA;
        watchedOnline = kirinOsOnline;
        since = last = nowSeconds;
        played = 0.0;
    }
    // 再生の長さは画面が動いているあいだの再生中の時間（1 回に 1 秒を超えて足さない：画面が閉じていた間は数えない）。
    if (playing) played += std::clamp (nowSeconds - last, 0.0, 1.0);
    last = nowSeconds;
    const auto waited = nowSeconds - since;
    using Budget = PreparationBudget;
    if (measuringA && slot == 2)
        return played > Budget::checkALevelPlaySeconds
            ? juce::String ("CHECKING") : juce::String();
    if (measuringA)
        return played > Budget::aLevelPlaySeconds ? juce::String ("CHECKING")
                                                  : juce::String();
    // 段階ごとの上限と、超えたときの「理由 / 直し方」は段階の表（HyphaReferenceStages.h）。利用者の操作を待つ段階に上限は無い。
    const auto& stage = stageOf (step);
    if (stage.budget == WaitBudget::none || (stage.budget == WaitBudget::kirinOsResponse && ! kirinOsOnline))
        return {};
    const auto counted = stage.budget == WaitBudget::alignmentPlay ? played : waited;
    return (stage.budget == WaitBudget::pages ? counted >= budgetSeconds (stage.budget)
                                            : counted > budgetSeconds (stage.budget)) ? juce::String (stage.overdue) : juce::String();
}
}
