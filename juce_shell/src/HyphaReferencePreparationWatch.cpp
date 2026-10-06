#include "HyphaReferencePreparationWatch.h"

#include "HyphaReferenceStages.h"

#include <algorithm>

namespace hypha::reference_ui
{
juce::String PreparationWatch::observe (int slot, SourceStep step, bool measuringA, bool kirinOsOnline, bool playing,
                                        double nowSeconds)
{
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
            ? juce::String ("A LEVEL NOT MEASURED IN 35 S OF PLAY / PLAY A LONGER, THEN SELECT AGAIN") : juce::String();
    if (measuringA)
        return played > Budget::aLevelPlaySeconds ? juce::String ("A LEVEL NOT MEASURED IN 10 S OF PLAY / PLAY A LONGER, THEN SELECT AGAIN")
                                                  : juce::String();
    // 段階ごとの上限と、超えたときの「理由 / 直し方」は段階の表（HyphaReferenceStages.h）。利用者の操作を待つ段階に上限は無い。
    const auto& stage = stageOf (step);
    if (stage.budget == WaitBudget::none || (stage.budget == WaitBudget::kirinOsResponse && ! kirinOsOnline))
        return {};
    const auto counted = stage.budget == WaitBudget::alignmentPlay ? played : waited;
    return counted > budgetSeconds (stage.budget) ? juce::String (stage.overdue) : juce::String();
}
}
