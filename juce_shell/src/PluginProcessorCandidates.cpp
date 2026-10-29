#include "PluginProcessor.h"
#include "kirin_hypha_identity_v2.h"

juce::Array<KirinHyphaProcessorBase::PreCandidate> KirinHyphaProcessorBase::enumeratePreCandidates() const
{
    juce::Array<PreCandidate> out;
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return out;

    constexpr size_t kCap = 32; // generous; FFI truncates beyond this
    KirinPreCandidateV2 buf[kCap];
    const size_t n = kirin_hypha_enumerate_pre_candidates_v2 (hyphaHandle, buf, kCap);
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
    KirinPostPairClaimV2 buf[kCap];
    const size_t n = kirin_hypha_enumerate_post_pair_claims_v2 (hyphaHandle, buf, kCap);
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


hypha::pair_preview::Ticket KirinHyphaProcessorBase::createPairPreview() const
{
    const juce::ScopedLock lock (handleLock);
    return hypha::pair_preview::Ticket (kirin_hypha_pair_preview_create (hyphaHandle));
}

bool KirinHyphaProcessorBase::pairPreviewMatches (const KirinPairPreview* preview) const
{
    const juce::ScopedLock lock (handleLock);
    return kirin_hypha_pair_preview_matches (hyphaHandle, preview);
}

bool KirinHyphaProcessorBase::automaticPairNeedsService() const
{
    const juce::ScopedLock pairLock (automaticPair.lock);
    return role == Role::Post && writesEnabled.load (std::memory_order_acquire)
        && ! automaticPair.inhibited.load (std::memory_order_acquire)
        && ! stateInformationSeen.load (std::memory_order_acquire)
        // outputStates includes Record, Keep, LISTEN, BLIND and other output owners.
        && ! isPlaying() && ! isHostRecording() && outputStates() == 0 && persistPairName.isEmpty() && persistPairInstanceId.isEmpty()
        && persistPairProjectHash.isEmpty() && pairStatus() == KIRIN_PAIR_STATUS_UNPAIRED;
}

bool KirinHyphaProcessorBase::tryAutomaticPair (const KirinPairPreview* preview,
                                                 const KirinPairPreviewValue& observed)
{
    if (! automaticPairNeedsService()) return false;
    bool changed = false;
    {
        const juce::ScopedLock pairLock (automaticPair.lock);
        const auto eligible = [this] { return automaticPairNeedsService(); };
        KirinPairPreviewValue current {};
        if (! eligible() || ! preview || ! pairPreviewMatches (preview)
            || ! kirin_hypha_pair_preview_poll (preview, &current) || ! current.complete
            || ! current.has_single || current.generation != observed.generation
            || juce::String::fromUTF8 (current.candidate.instance_id)
                != juce::String::fromUTF8 (observed.candidate.instance_id)) return false;
        // Re-read the menu's candidate and claim lists immediately before exact selection.
        const auto candidates = enumeratePreCandidates();
        if (candidates.size() >= 32) return false; // The fixed FFI buffer may be truncated.
        const auto claims = enumeratePostPairClaims();
        if (claims.size() >= 32) return false;
        const auto ownId = instanceId();
        const auto id = juce::String::fromUTF8 (current.candidate.instance_id);
        int available = 0;
        bool sameCandidate = false;
        for (const auto& candidate : candidates)
        {
            bool owned = false;
            for (const auto& claim : claims)
                if (claim.instanceId != ownId
                    && ((claim.hasPairedPreInstanceId && claim.pairedPreInstanceId == candidate.instanceId)
                        || (! claim.hasPairedPreInstanceId && claim.hasPairPreName && candidate.hasName
                            && candidate.name.isNotEmpty() && claim.pairPreName == candidate.name)))
                    owned = true;
            if (! owned) { ++available; sameCandidate = candidate.instanceId == id; }
        }
        KirinPairPreviewValue confirmed {};
        if (available != 1 || ! sameCandidate || ! pairPreviewMatches (preview)
            || ! kirin_hypha_pair_preview_poll (preview, &confirmed) || ! confirmed.complete
            || ! confirmed.has_single || confirmed.generation != current.generation
            || juce::String::fromUTF8 (confirmed.candidate.instance_id) != id || ! eligible()) return false;
        changed = applyPairCandidate (id, juce::String::fromUTF8 (current.candidate.name));
        if (changed) automaticPair.inhibited.store (true, std::memory_order_release);
    }
    if (changed) updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
    return changed;
}

void KirinHyphaProcessorBase::clearPairCandidate()
{
    automaticPair.inhibited.store (true, std::memory_order_release);
    {
        const juce::ScopedLock pairLock (automaticPair.lock);
        automaticPair.disconnected.store (true, std::memory_order_release);
        invalidateLiveComparePair();
        {
            const juce::ScopedLock lock (handleLock);
            if (hyphaHandle != nullptr)
                kirin_hypha_set_pair_target (hyphaHandle, "");
        }
        persistPairName.clear();
        persistPairProjectHash.clear();
        persistPairInstanceId.clear();
    }
    updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}
