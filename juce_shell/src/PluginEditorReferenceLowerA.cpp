#include "PluginEditor.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
#include "HyphaReferenceRuntimeView.h"

// 2026-10-03（R-12）：B・C・V の MATCH が上限（True Peak）を超える（大きな A に静かな参照曲）とき、
// 断るだけでなく「A を差だけ下げて合わせる」承認を出す。参照は元の音量のまま。承認した量は足元の RETURN で戻す
// まで保つ（live PRE/POST 比較の POST の減衰と同じ見せ方）。承認のボタンは 300% の REF のアクション。

namespace
{
const hypha::reference_audition::Snapshot* roleOf (const hypha::reference_audition::Snapshot& runtime, int slot)
{
    const auto& role = slot == 1 ? runtime.versionSelection : slot == 3 ? runtime.referenceSelection : runtime.checkSelection;
    return role != nullptr ? role.get() : nullptr;
}

bool needsLowerA (const hypha::reference_audition::Snapshot* role)
{
    return role != nullptr && role->matchFailure == hypha::reference_audition::MatchFailure::ceilingExceeded
        && role->neededAttenuationDb < 0.0 && role->playbackIdentity.isNotEmpty();
}
}

// その役の上限超えを、窓で一度だけ知らせる（押した直後、または待たせた役が合わせる時点で上限を超えたとき）。承認の
// ボタンは 300% の REF にあるので、その役のページにして大きさを合わせる。出したことは processor が持ち、窓を開き直しても
// 出し直さない。知らせたら true。
bool KirinHyphaEditor::offerReferenceLowerA (int slot, const hypha::reference_audition::Snapshot& role)
{
    if (! needsLowerA (&role)) return false;
    processorRef.markReferenceLowerAOfferShown (slot, role.matchFailureSerial);
    processorRef.selectReferenceVisualSlot (slot);
    if (getWidth() < 900 || getHeight() < 600) setSize (900, 600);
    showToast (juce::String (hypha::reference_ui::roleLetter (slot)) + " needs A "
               + juce::String (-role.neededAttenuationDb, 1) + " dB lower to match. Press LOWER A.");
    return true;
}

// REF のアクションの承認：ボタンに出した申し出（その役・その音・その MATCH の失敗）を、ボタンに出した量で承認する。
void KirinHyphaEditor::approveOfferedLowerA (const hypha::reference_audition::LowerAOffer& offer)
{
    using Approval = hypha::reference_audition::LowerAApproval;
    if (outputRefused (hypha::output_owner::Activity::lowerA)) return;
    const auto result = processorRef.approveReferenceLowerA (offer);
    if (result == Approval::lowered) referenceLowerAApprovedDb = offer.db;
    else if (result == Approval::postInUse)
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
    // 承認の後に A が大きくなり、鳴らす時点の差まで深く下げ直したら一度だけ知らせる（承認した量と違うので）。
    if (referenceLowerAApprovedDb < 0.0 && (runtime.bSelected || runtime.heldAttenuationDb >= 0.0))
    {
        if (runtime.bSelected && runtime.heldAttenuationDb < referenceLowerAApprovedDb - 0.05)
            showToast ("A lowered " + juce::String (-runtime.heldAttenuationDb, 1) + " dB to match: A got louder after the offer.");
        referenceLowerAApprovedDb = 0.0;
    }
    // 待たせた役（A がたまる前・準備中に押した役）が合わせる時点で上限を超えたら、押し直させずに一度だけ知らせる
    // （2026-10-04）。REF のページが出ていないときは、窓の大きさもページも変えない（開いたときにボタンが言う）。
    using Stage = hypha::reference_audition::PendingAuditionView::Stage;
    const auto& pending = runtime.pendingAudition;
    if (pending.stage == Stage::ceilingExceeded && referenceView.isShowing())
        if (const auto* role = roleOf (runtime, pending.slot); needsLowerA (role)
            && ! (runtime.lowerAOfferShownSlot == pending.slot && runtime.lowerAOfferShownSerial == role->matchFailureSerial))
            offerReferenceLowerA (pending.slot, *role);
    const int slot = state.comparisonSlot;
    const auto found = hypha::reference_ui::lowerAOfferFor (runtime, slot);
    if (! found) return;
    const auto offer = *found;
    const auto letter = juce::String (hypha::reference_ui::roleLetter (slot));
    const auto amount = juce::String (-offer.db, 1);
    // 直し方はボタンが言う（量も）。2 度言うと 300% の足元で状態の文とボタンの文が両方切れた（2026-10-05）。
    state.status = letter + " NEEDS A " + amount + " DB LOWER";
    state.action = { hypha::reference_ui::ActionKind::lowerAAndPlay, offer };
    state.actionText = "LOWER A " + amount + " DB & PLAY " + letter;
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
