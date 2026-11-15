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
    // INV-AB6: after an A/B return that ended at its deadline, B may still carry the comparison's
    // audio. Nothing of it is recorded: the entry waits (the editor keeps the press) until an ordinary
    // marker has passed the chain and four seconds have settled, so the take and its pre-roll are ordinary.
    if (candidateTailPending()) return false;
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_keep (hyphaHandle);
}

bool KirinHyphaProcessorBase::keepAll (std::optional<std::uint64_t> ticket, bool resume)
{
    const juce::ScopedLock pairLock (automaticPair.lock);
    if (ticket && *ticket != restoreOrPairTicket()) return false;
    refreshLicenseForUserAction();
    if (outputDecision (hypha::output_owner::Activity::recordKeep).refused()) return false;
    if (candidateTailPending()) return false; // INV-AB6, as keepPair above
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return resume ? kirin_hypha_keep_all_resume (hyphaHandle) : kirin_hypha_keep_all (hyphaHandle);
}

bool KirinHyphaProcessorBase::allKeepTailPending() const
{
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle == nullptr || kirin_hypha_all_keep_tail_pending (hyphaHandle);
}
