#include "PluginProcessor.h"

// Keep（PRE と組の Record）・Keep ALL。出力の持ち主の表で先に確かめ（Blind・鳴っている B・C・V のあいだは断る。
// INV-S33 の mono・stereo だけ）、通ったら Rust が最後に確かめる。
bool KirinHyphaProcessorBase::keepPair()
{
    refreshLicenseForUserAction();
    if (outputDecision (hypha::output_owner::Activity::recordKeep).refused()) return false;
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_keep (hyphaHandle);
}

bool KirinHyphaProcessorBase::keepAll()
{
    refreshLicenseForUserAction();
    if (outputDecision (hypha::output_owner::Activity::recordKeep).refused()) return false;
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_keep_all (hyphaHandle);
}
