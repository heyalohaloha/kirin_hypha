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
    char out[64] = { 0 };
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
    char projectOut[64] = { 0 };
    char instanceOut[64] = { 0 };
    if (! kirin_hypha_get_paired_pre_locator (
            hyphaHandle, projectOut, sizeof (projectOut), instanceOut, sizeof (instanceOut)))
        return false;
    projectHash = juce::String::fromUTF8 (projectOut);
    instanceId = juce::String::fromUTF8 (instanceOut);
    return true;
}

