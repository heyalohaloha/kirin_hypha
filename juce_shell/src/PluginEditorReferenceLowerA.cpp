#include "PluginEditor.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
#include "HyphaReferenceRuntimeView.h"
#include "HyphaReferenceNotices.h"

// 2026-10-03（R-12）：B・C・V の MATCH が上限（True Peak）で届かないとき（大きな A に静かな参照曲）、役は上限まで
// 上げて鳴らし（2026-11-04、押した役を止めない）、「A を差だけ下げて合わせる」承認を出す。参照は元の音量のまま。承認した
// 量は足元の RETURN で戻すまで保つ（live PRE/POST 比較の POST の減衰と同じ見せ方）。承認のボタンは 300% の REF のアクション。

// REF のアクションの承認：ボタンに出した申し出（その役・その音・その MATCH の失敗）を、ボタンに出した量で承認する。
void KirinHyphaEditor::approveOfferedLowerA (const hypha::reference_audition::LowerAOffer& offer)
{
    using Approval = hypha::reference_audition::LowerAApproval;
    if (outputRefused (hypha::output_owner::Activity::lowerA)) return;
    const auto result = processorRef.approveReferenceLowerA (offer);
    if (result == Approval::lowered) return;
    if (result == Approval::postInUse)
        showToast ("PRE / POST LISTEN is using POST. End it or press RETURN, then press the role again.");
    else if (result == Approval::stale)
        showToast ("The offer changed. Press the role again.");
    else
        showToast ("A was not lowered. Press the role again.");
}

// 下げている量（読みは下げた後の A の基準にする）と、見ているページの役の承認を状態に入れる。承認の申し出は見ている
// ページの役の MATCH の結果から作る（別の役の申し出を、このページのボタンで承認しない）。
void KirinHyphaEditor::applyReferenceLowerA (hypha::reference_ui::State& state,
                                             const hypha::reference_audition::Snapshot& runtime)
{
    state.heldAttenuationDb = runtime.heldAttenuationDb;
    const int slot = state.comparisonSlot;
    const auto found = hypha::reference_ui::lowerAOfferFor (runtime, slot);
    if (! found) return;
    const auto offer = *found;
    // 足りない量は状態の行が言う（A との差。ボタンは下げる量）。
    state.action = { hypha::reference_ui::ActionKind::lowerAAndPlay, offer };
    state.actionText = hypha::reference_ui::notice::lowerAOfferAction (offer.db);
}

// 足元の RETURN：live 比較が POST を下げていなくて Reference が A を下げていれば、Reference の分を戻す。
bool KirinHyphaEditor::returnReferenceLevelIfHeld()
{
    const auto live = processorRef.liveCompareStatus();
    if (live.postTarget < 1.0f || live.postActual < 1.0f || processorRef.referenceHeldAttenuationDb() >= 0.0)
        return false;
    processorRef.returnReferenceLevelToNormal();
    return true;
}

// 足元に出す Reference の下げ幅（0.1 dB 単位、0 以下）。live 比較の減衰と同じ欄に出す（同時には起きない）。
int KirinHyphaEditor::referenceHeldTenthsDb() const
{
    const auto held = processorRef.referenceHeldAttenuationDb();
    return held < 0.0 ? juce::jmin (-1, juce::roundToInt (10.0 * held)) : 0;
}
#endif
