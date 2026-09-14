#include "PluginProcessor.h"

juce::Array<KirinHyphaProcessorBase::PreCandidate> KirinHyphaProcessorBase::enumeratePreCandidates() const
{
    juce::Array<PreCandidate> out;
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return out;

    constexpr size_t kCap = 32; // generous; FFI truncates beyond this
    KirinPreCandidate buf[kCap];
    const size_t n = kirin_hypha_enumerate_pre_candidates (hyphaHandle, buf, kCap);
    for (size_t i = 0; i < n; ++i)
    {
        PreCandidate c;
        c.instanceId = juce::String::fromUTF8 (buf[i].instance_id);
        c.name       = juce::String::fromUTF8 (buf[i].name);
        c.hasName    = (buf[i].has_name != 0);
        out.add (c);
    }
    return out;
}

juce::Array<KirinHyphaProcessorBase::PostPairClaim> KirinHyphaProcessorBase::enumeratePostPairClaims() const
{
    juce::Array<PostPairClaim> out;
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return out;

    constexpr size_t kCap = 32; // same cap as PRE candidates; FFI truncates beyond this
    KirinPostPairClaim buf[kCap];
    const size_t n = kirin_hypha_enumerate_post_pair_claims (hyphaHandle, buf, kCap);
    for (size_t i = 0; i < n; ++i)
    {
        PostPairClaim c;
        c.instanceId      = juce::String::fromUTF8 (buf[i].instance_id);
        c.pairPreName     = juce::String::fromUTF8 (buf[i].pair_pre_name);
        c.hasPairPreName  = (buf[i].has_pair_pre_name != 0);
        c.pairedPreInstanceId = juce::String::fromUTF8 (buf[i].paired_pre_instance_id);
        c.hasPairedPreInstanceId = (buf[i].has_paired_pre_instance_id != 0);
        out.add (c);
    }
    return out;
}

