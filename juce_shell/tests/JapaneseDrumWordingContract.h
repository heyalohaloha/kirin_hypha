#pragma once

#include "../src/HyphaAttackV2Model.h"
#include "../src/HyphaLanguage.h"
#include <iostream>

namespace hypha::tests
{
inline bool verifyJapaneseDrumWording()
{
    using namespace attack_v2;
    struct Reason { std::uint8_t code; const char* english; const char* japanese; };
    const Reason reasons[] {
        { KIRIN_REASON_SILENT, "Silent", u8"帯域に音なし" },
        { KIRIN_REASON_BOTH_SILENT, "Both silent", u8"両側に音なし" },
        { KIRIN_REASON_RINGING, "Ringing", u8"余韻あり" },
        { KIRIN_REASON_NEXT_HIT, "Next hit", u8"次の打音" },
        { KIRIN_REASON_WAITING_PUBLICATION, "Waiting for publication", u8"結果の反映待ち" },
        { KIRIN_REASON_SEMANTICS, "Update PRE", u8"PREを更新" },
        { KIRIN_REASON_LONG_TAIL, "Long tail", u8"余韻が長い" },
    };
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto& reason : reasons)
            if (reasonText (reason.code, true) != juce::String::fromUTF8 (
                    language == i18n::Language::japanese ? reason.japanese : reason.english))
            {
                std::cerr << "DRUM reason wording: " << reason.english << '\n';
                return false;
            }
    }
    const i18n::ScopedLanguage scoped (i18n::Language::japanese);
    if (finishText (KIRIN_FINISH_NOT_KEPT) != juce::String::fromUTF8 ("Not retained")) return false;
    Lane lane;
    lane.unbounded = true;
    if (laneReason (lane) != juce::String::fromUTF8 (u8"Unbounded")) return false;
    lane.unbounded = false; lane.number.valid = true; lane.scope = Scope::wholeInterval;
    lane.withinResolution = true;
    return laneReason (lane) == juce::String::fromUTF8 (u8"RES内");
}
}
