#pragma once

#include "reference_audition/ReferenceAuditionController.h"
#include "reference_audition/ReferenceHeldAttenuation.h"

#include <optional>

#include <cstdint>

// REF のアクションボタン。表示の文と、押したときにすることを、同じ所で一緒に決める（押したときはこの値だけを実行し、
// ほかの印から推し量らない）。
namespace hypha::reference_ui
{
enum class ActionKind : std::uint8_t
{
    none,
    lowerAAndPlay,              // 上限を超えた MATCH：A を下げて、その役を鳴らす（承認）
    approveBlindLowerA,         // VERSION BLIND：A を下げて始める（承認）
    retryPresetPreparation,     // Kirin OS の CHECK SET（Preset）の準備をやり直す
    retryCandidatePreparation,  // Kirin OS の曲（C の候補）の準備をやり直す
    openReference,              // Kirin OS でその Preset を開く
    chooseSource,               // Kirin OS で音源を選び直す
    measureSource,              // Kirin OS で音源を測る
    retryKirinOs,               // Kirin OS へもう一度頼む
};

using LowerAOffer = reference_audition::LowerAOffer;

struct ActionIntent
{
    ActionKind kind = ActionKind::none;
    LowerAOffer offer;  // lowerAAndPlay のとき
};

// 見ているページの役の、A を下げる承認の申し出。その役の MATCH が上限を超えていて、ほかの役が鳴っていないときだけ
// （申し出はページの役から作る：別の役の申し出を、このページのボタンで承認しない）。
inline std::optional<LowerAOffer> lowerAOfferFor (const reference_audition::Snapshot& runtime, int slot)
{
    const auto& role = slot == 1 ? runtime.versionSelection : slot == 3 ? runtime.referenceSelection
                     : slot == 2 ? runtime.checkSelection : nullptr;
    if (role == nullptr || role->matchFailure != reference_audition::MatchFailure::ceilingExceeded
        || ! (role->neededAttenuationDb < 0.0) || role->playbackIdentity.isEmpty()
        || (runtime.bSelected && runtime.audibleComparisonSlot != slot))
        return std::nullopt;
    return LowerAOffer { slot, role->neededAttenuationDb, role->playbackIdentity, role->selectionGeneration,
                         role->matchFailureSerial };
}

// OPEN REFERENCE は Kirin OS の Preset（C）を指すときだけ。V（Version の項目）・B の曲・保存した選択が無いとき
// （SAVED CHOICE UNAVAILABLE）には出さない：Kirin OS は Preset として見つけられない（直し方は状態の行が言う）。
inline bool opensKirinOsPreset (const reference_audition::Snapshot& runtime)
{
    return runtime.libraryReceived && runtime.comparisonSlot == 2 && runtime.presetId.isNotEmpty()
        && runtime.rejectionCode != "reference_selection_unavailable"
        && (runtime.state == reference_audition::RuntimeState::rejected || runtime.state == reference_audition::RuntimeState::waiting);
}
}
