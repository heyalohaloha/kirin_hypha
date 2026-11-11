#include "PluginProcessor.h"

// Keep（PRE と組の Record）・Keep ALL。出力の持ち主の表で先に確かめ（Blind・鳴っている B・C・V のあいだは断る。
// INV-S33 の mono・stereo だけ）、通ったら Rust が最後に確かめる。
// `ticket` is the restore and pairing generation of a press that waited: a restore or a new pair since then
// starts nothing (checked under the pair lock, as LISTEN's waited press is).
bool KirinHyphaProcessorBase::keepPair (std::optional<std::uint64_t> ticket)
{
    const juce::ScopedLock pairLock (automaticPair.lock);
    if (ticket && *ticket != restoreOrPairTicket()) return false;
    refreshLicenseForUserAction();
    if (outputDecision (hypha::output_owner::Activity::recordKeep).refused()) return false;
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_keep (hyphaHandle);
}

bool KirinHyphaProcessorBase::keepAll (std::optional<std::uint64_t> ticket)
{
    const juce::ScopedLock pairLock (automaticPair.lock);
    if (ticket && *ticket != restoreOrPairTicket()) return false;
    refreshLicenseForUserAction();
    if (outputDecision (hypha::output_owner::Activity::recordKeep).refused()) return false;
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_keep_all (hyphaHandle);
}
