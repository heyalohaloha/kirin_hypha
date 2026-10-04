#include "PluginEditor.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
#include "HyphaReferenceRuntimeView.h"

// 2026-10-03（Daisuke 承認、R-12）：B・C・V の MATCH が上限（True Peak）を超える（大きな A に静かな参照曲）とき、
// 断るだけでなく「A を差だけ下げて合わせる」承認を出す。参照は元の音量のまま。承認した量は足元の RETURN で戻す
// まで保つ（live PRE/POST 比較の POST の減衰と同じ見せ方）。承認のボタンは 300% の REF のアクション。

// 押した役（再生中・MATCH のやり直し）が上限を超えたら、承認を出す。出したら true（通知はここで出す）。
bool KirinHyphaEditor::offerReferenceLowerA (int slot, const hypha::reference_audition::Snapshot& role)
{
    if (role.matchFailure != hypha::reference_audition::MatchFailure::ceilingExceeded
        || ! (role.neededAttenuationDb < 0.0))
        return false;
    referenceLowerAOffer = { slot, role.neededAttenuationDb };
    processorRef.selectReferenceVisualSlot (slot);
    if (getWidth() < 900 || getHeight() < 600) setSize (900, 600);  // 承認のボタンは 300% の REF にある
    showToast (juce::String (hypha::reference_ui::roleLetter (slot)) + " needs A "
               + juce::String (-role.neededAttenuationDb, 1) + " dB lower to match. Press LOWER A.");
    return true;
}

// REF のアクション。出している承認があれば、ボタンに出した量で承認する（A を下げ終わってから、押した役を鳴らす）。
bool KirinHyphaEditor::approveOfferedLowerA()
{
    using Approval = hypha::reference_audition::LowerAApproval;
    const auto offer = referenceLowerAOffer;
    if (offer.slot == 0) return false;
    referenceLowerAOffer = {};
    const auto result = processorRef.approveReferenceLowerA (offer.slot, offer.db);
    if (result == Approval::lowered) referenceLowerAApprovedDb = offer.db;
    if (result == Approval::postInUse)
        showToast ("PRE / POST LISTEN is using POST. End it or press RETURN, then press the role again.");
    else if (result != Approval::lowered)
        showToast ("A was not lowered. Press the role again.");
    return true;
}

// 下げている量（読みは下げた後の A の基準にする）と、見ている役に出している承認を状態に入れる。選び直した・
// 何かを鳴らした（その役の MATCH の結果が変わった）ら、承認は引っ込める。
void KirinHyphaEditor::applyReferenceLowerA (hypha::reference_ui::State& state,
                                             const hypha::reference_audition::Snapshot& runtime)
{
    state.heldAttenuationDb = runtime.heldAttenuationDb;
    // 承認の後に A が大きくなり、鳴らす時点の差まで深く下げ直したら一度だけ知らせる（承認した量と違うので）。
    if (referenceLowerAApprovedDb < 0.0 && (runtime.bSelected || runtime.heldAttenuationDb >= 0.0))
    {
        if (runtime.bSelected && runtime.heldAttenuationDb < referenceLowerAApprovedDb - 0.05)
            showToast ("A lowered " + juce::String (-runtime.heldAttenuationDb, 1) + " dB to match: A got louder after the offer.");
        referenceLowerAApprovedDb = 0.0;
    }
    auto& offer = referenceLowerAOffer;
    // 待たせた役（A がたまる前・準備中に押した役）が、合わせる時点で上限を超えて止まったときも、押し直させずに
    // 一度だけ承認を出す（2026-10-04）。
    using Stage = hypha::reference_audition::PendingAuditionView::Stage;
    const auto& pending = runtime.pendingAudition;
    // 別の役を押して待たせたら、前の役の承認は引っ込める（押した役が優先）。2026-10-04 の実機：B の承認が残って
    // いて、あとで押して待たせた C が上限を超えても承認が出ず「C STOPPED / MATCH EXCEEDS SAFE LEVEL」で止まった。
    if (offer.slot != 0 && pending.stage != Stage::none && pending.slot != 0 && pending.slot != offer.slot) offer = {};
    if (pending.stage != Stage::ceilingExceeded) referenceLowerAPendingOffered = 0;
    else if (offer.slot == 0 && referenceLowerAPendingOffered != pending.slot)
    {
        referenceLowerAPendingOffered = pending.slot;
        const auto& role = pending.slot == 1 ? runtime.versionSelection
                         : pending.slot == 3 ? runtime.referenceSelection : runtime.checkSelection;
        if (role != nullptr) offerReferenceLowerA (pending.slot, *role);
    }
    if (offer.slot != 0)
    {
        const auto& role = offer.slot == 1 ? runtime.versionSelection
                         : offer.slot == 3 ? runtime.referenceSelection : runtime.checkSelection;
        // 別の役が鳴った・選び直した（その役の MATCH の結果が変わった）ら引っ込める。C の MATCH のやり直しは C が
        // 鳴ったまま出す。
        if ((runtime.bSelected && runtime.audibleComparisonSlot != offer.slot) || role == nullptr
            || role->matchFailure != hypha::reference_audition::MatchFailure::ceilingExceeded)
            offer = {};
    }
    state.lowerAOfferSlot = offer.slot;
    state.lowerAOfferDb = offer.db;
    if (offer.slot == 0 || offer.slot != state.comparisonSlot) return;
    const auto letter = juce::String (hypha::reference_ui::roleLetter (offer.slot));
    const auto amount = juce::String (-offer.db, 1);
    // 直し方はボタンが言う（量も）。2 度言うと 300% の足元で状態の文とボタンの文が両方切れた（2026-10-05、Mac の実機）。
    state.status = letter + " NEEDS A " + amount + " DB LOWER";
    state.actionText = "LOWER A " + amount + " DB & PLAY " + letter;
}

// 足元の RETURN：live 比較が POST を下げていなくて Reference が A を下げていれば、Reference の分を戻す。
bool KirinHyphaEditor::returnReferenceLevelIfHeld()
{
    const auto live = processorRef.liveCompareStatus();
    if (live.postTarget < 1.0f || live.postActual < 1.0f || processorRef.referenceHeldAttenuationDb() >= 0.0)
        return false;
    referenceLowerAOffer = {};
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
