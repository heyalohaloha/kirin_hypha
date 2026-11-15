#include "PluginProcessor.h"

int KirinHyphaProcessorBase::pairStatus() const
{
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr ? (int) kirin_hypha_pair_status (hyphaHandle) : 0;
}

juce::String KirinHyphaProcessorBase::pairedPreInstanceId() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return {};
    char out[65] = { 0 };
    return kirin_hypha_get_paired_pre_instance_id (hyphaHandle, out, sizeof (out))
             ? juce::String::fromUTF8 (out)
             : juce::String();
}

bool KirinHyphaProcessorBase::pairedPreLocator (juce::String& projectHash,
                                                juce::String& instanceId) const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    char projectOut[65] = { 0 };
    char instanceOut[65] = { 0 };
    if (! kirin_hypha_get_paired_pre_locator (
            hyphaHandle, projectOut, sizeof (projectOut), instanceOut, sizeof (instanceOut)))
        return false;
    projectHash = juce::String::fromUTF8 (projectOut);
    instanceId = juce::String::fromUTF8 (instanceOut);
    return true;
}

// A queued press must see the pair and engine replacement under the same ownership locks.
KirinHyphaProcessorBase::PairWaitSnapshot KirinHyphaProcessorBase::pairWaitSnapshot() const
{
    const juce::ScopedLock pairLock (automaticPair.lock);
    const juce::ScopedLock engineLock (handleLock);
    PairWaitSnapshot result;
    result.ticket = restoreOrPairTicket();
    char out[65] = {};
    result.paired = hyphaHandle != nullptr
        && kirin_hypha_get_paired_pre_instance_id (hyphaHandle, out, sizeof (out));
    const auto id = result.paired ? juce::String::fromUTF8 (out) : persistPairInstanceId;
    result.key = id + "|" + pairDisplayName() + "|" + juce::String ((juce::int64) result.ticket);
    result.stable = ! liveCompare.authority.restoring() && result.ticket == restoreOrPairTicket();
    return result;
}
