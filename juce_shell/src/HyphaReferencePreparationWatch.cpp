#include "HyphaReferencePreparationWatch.h"

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
    switch (step)
    {
        case SourceStep::waitingForKirinOs:
            return kirinOsOnline && waited > Budget::kirinOsResponseSeconds
                ? juce::String ("KIRIN OS IS NOT RESPONDING / OPEN KIRIN OS") : juce::String();
        case SourceStep::verifyingSource:
            return waited > Budget::preparationSeconds ? juce::String ("SOURCE NOT VERIFIED IN 10 S / CHECK THE SOURCE IN KIRIN OS")
                                                       : juce::String();
        case SourceStep::loadingAudio:
            return waited > Budget::preparationSeconds ? juce::String ("AUDIO NOT LOADED IN 10 S / PLAY FROM ANOTHER POSITION")
                                                       : juce::String();
        case SourceStep::preparing:
            return waited > Budget::preparationSeconds ? juce::String ("NOT PREPARED IN 10 S / OPEN THE SOURCE IN KIRIN OS")
                                                       : juce::String();
        case SourceStep::aligning:
            return played > Budget::alignmentPlaySeconds ? juce::String ("NO MATCH IN 30 S OF PLAY / CHOOSE THE VERSION AGAIN")
                                                         : juce::String();
        case SourceStep::ready:
        case SourceStep::registerVersion:
        case SourceStep::chooseVersion:
        case SourceStep::enableCheck:
        case SourceStep::chooseSource:
        case SourceStep::playDaw:
        case SourceStep::noMatchingPassage:
        case SourceStep::playAnotherPassage:
        case SourceStep::outsideCue:
        case SourceStep::attention:
            break;  // 聴ける、または利用者の操作を待っている（上限は無い。直し方は今の文にある）
    }
    return {};
}
}
