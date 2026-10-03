#include "PluginProcessor.h"

// 2026-10-03（Daisuke 承認、R-12）：B・C・V の MATCH が上限（True Peak）を超えるとき、利用者が承認すれば
// A（POST の出力全体）を差だけ下げて合わせる。参照は元の音量のまま。承認した量は試聴の後も、利用者が RETURN で
// 戻すまで保つ。live 比較・Blind が POST を取っている・下げているあいだは下げない（POST を二重に下げない）。

bool KirinHyphaProcessorBase::approveReferenceLowerA (int slot)
{
    refreshLicenseForUserAction();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! licenseIsOs() || referenceAuditionController == nullptr || role != Role::Post) return false;
    if (liveCompare.sessionActive.load (std::memory_order_acquire)
        || liveCompareAdmission (false, false) != hypha::live_compare::StartResult::started)
        return false;
    const bool accepted = referenceAuditionController->approveLowerAAndPlay (slot);
    if (accepted) startTimer (50);  // 下げ終わってから鳴らす待ちを回す
    return accepted;
   #else
    juce::ignoreUnused (slot);
    return false;
   #endif
}

void KirinHyphaProcessorBase::returnReferenceLevelToNormal()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController != nullptr) referenceAuditionController->returnAToNormalLevel();
   #endif
}

double KirinHyphaProcessorBase::referenceHeldAttenuationDb() const noexcept
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return referenceAuditionController != nullptr ? referenceAuditionController->heldAttenuationDb() : 0.0;
   #else
    return 0.0;
   #endif
}
