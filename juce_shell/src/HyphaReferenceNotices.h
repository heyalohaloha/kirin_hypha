#pragma once

#include "HyphaReferenceComponent.h"
#include "HyphaReferenceRuntimeView.h"

#include <algorithm>

// 2026-10-06：値を入れて組み立てる Reference の文（足元に一度出す知らせと、A を下げる承認の申し出）。editor はここで
// 作った文だけを出し、試験は同じ関数で全部の組み合わせを作って、日本語の画面に英語が残らないかを確かめる。
namespace hypha::reference_ui::notice
{
// 押した役がまだ鳴らせない理由：MATCH の失敗があればそれ、無ければその役の段階（「V: Preparing」）。
inline juce::String roleUnavailable (int slot, SourceStep step, reference_audition::MatchFailure failure)
{
    if (const auto text = runtime_view::matchFailureText (failure); text.isNotEmpty()) return text;
    const juce::String letter (roleLetter (slot));
    return step == SourceStep::ready ? letter + " could not switch at this playhead. A remains live; retry when " + letter + " is ready."
                                     : letter + ": " + stepText (step);
}

// 上限を超えた MATCH：A を下げれば合うことと、押すボタン。
inline juce::String lowerANeeded (int slot, double neededAttenuationDb)
{
    return juce::String (roleLetter (slot)) + " needs A " + juce::String (-neededAttenuationDb, 1) + " dB lower to match. Press LOWER A.";
}

// 申し出の状態の文（量だけ。直し方はボタンが言う）と、ボタンの文。
inline juce::String lowerAOfferStatus (int slot, double neededAttenuationDb)
{
    return juce::String (roleLetter (slot)) + " NEEDS A " + juce::String (-neededAttenuationDb, 1) + " DB LOWER";
}

inline juce::String lowerAOfferAction (int slot, double neededAttenuationDb)
{
    return "LOWER A " + juce::String (-neededAttenuationDb, 1) + " DB & PLAY " + juce::String (roleLetter (slot));
}

// 承認の後に A が大きくなり、承認した量より深く下げ直した。
inline juce::String lowerAAgain (double heldAttenuationDb)
{
    return "A lowered " + juce::String (-heldAttenuationDb, 1) + " dB to match: A got louder after the offer.";
}

// 追従が止まった（上限、または MATCH から ±6 dB）。
inline juce::String trackingStopped (bool atCeiling)
{
    return atCeiling ? "Level follow stopped at the safe ceiling. The current gain is kept."
                     : "Level follow stopped 6 dB from the MATCH. The current gain is kept.";
}

// Kirin OS の項目を外した：最初の名前と数、なぜ（名前の字）と直し方（Kirin OS で名前を直す。ほかは準備し直す）。
inline juce::String librarySkipped (const std::vector<reference_audition::RuntimeSkippedItem>& items)
{
    if (items.empty()) return {};
    const auto named = std::find_if (items.begin(), items.end(), [] (const auto& item) { return item.name.isNotEmpty(); });
    const bool names = std::all_of (items.begin(), items.end(), [] (const auto& item) { return item.nameUnreadable; });
    const auto count = static_cast<int> (items.size());
    if (named == items.end())
        return count == 1 ? juce::String ("A Kirin OS item was not read. Prepare it again in Kirin OS.")
                          : juce::String (count) + " Kirin OS items were not read. Prepare them again in Kirin OS.";
    const auto name = "\"" + named->name + "\"";
    if (count == 1)
        return names ? name + ": name not readable. Rename it in Kirin OS." : name + " was not read. Prepare it again in Kirin OS.";
    const auto more = juce::String (count - 1);
    return names ? name + " and " + more + " more: names not readable. Rename them in Kirin OS."
                 : name + " and " + more + " more were not read. Prepare them again in Kirin OS.";
}

// 300% 未満で C・V を押して、その役の画面を開いた。
inline juce::String pageOpened (int slot, bool playable)
{
    const juce::String letter (roleLetter (slot));
    return playable ? letter + " opened at 300%. Press " + letter + " to listen." : letter + " opened at 300%.";
}
}
