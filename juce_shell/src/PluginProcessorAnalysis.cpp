#include "PluginProcessor.h"
#include "HyphaAnalysisFfiAdapter.h"

void KirinHyphaProcessorBase::normalizeSpectrumSelectionForInputChannels (int channels) noexcept
{
    if (role != Role::Post || channels == 2)
        return;
    if (preferredSpectrumChannelMode.load (std::memory_order_acquire)
            == KIRIN_SPECTRUM_CHANNEL_SIDE)
        preferredSpectrumChannelMode.store (
            KIRIN_SPECTRUM_CHANNEL_LR, std::memory_order_release);
    if (preferredSpectrumDisplaySelection.load (std::memory_order_acquire)
            == KIRIN_SPECTRUM_SELECTION_MID_SIDE)
        preferredSpectrumDisplaySelection.store (
            preferredSpectrumChannelMode.load (std::memory_order_acquire),
            std::memory_order_release);

    auto demand = requestedAnalysisDemand();
    if (demand.kind == hypha::analysis::Kind::midSideSpectrum)
    {
        demand.kind = hypha::analysis::Kind::spectrum;
        demand.channelMode = preferredSpectrumChannelMode.load (std::memory_order_acquire);
        analysisDemandOwner.replaceCurrentRequest (demand);
        analysisApplication.requestChanged();
    }
    else if (hypha::analysis::usesChannelMode (demand.kind)
             && demand.channelMode == KIRIN_SPECTRUM_CHANNEL_SIDE)
    {
        demand.channelMode = KIRIN_SPECTRUM_CHANNEL_LR;
        analysisDemandOwner.replaceCurrentRequest (demand);
        analysisApplication.requestChanged();
    }
}

std::uint64_t KirinHyphaProcessorBase::beginAnalysisUiSession() noexcept
{
    if (role != Role::Post)
        return 0;
    const juce::ScopedLock sl (handleLock);
    const auto owner = analysisDemandOwner.begin();
    analysisApplication.requestChanged();
    if (hyphaHandle != nullptr && writesEnabled.load (std::memory_order_acquire))
        serviceRequestedAnalysisUnderHandleLock();
    return owner;
}

bool KirinHyphaProcessorBase::serviceRequestedAnalysisUnderHandleLock()
{
    if (hyphaHandle == nullptr || ! writesEnabled.load (std::memory_order_acquire))
        return false;
    const auto demand = requestedAnalysisDemand();
    if (! analysisApplication.shouldApply (demand))
        return analysisApplication.isApplied (demand);

    hypha::analysis::ShippingFfiAdapter adapter { hyphaHandle };
    const auto previous = analysisApplication.previousForCurrentEngine();
    const bool accepted = hypha::analysis::apply (previous, demand, adapter);

    if (accepted)
        analysisApplication.applicationSucceeded (demand);
    else
    {
        // Failure must never become an applied-state claim. Both stop calls are idempotent and
        // leave the existing Rust Coordinator as the sole owner of process-wide slot admission.
        kirin_hypha_set_spectrum_visible (hyphaHandle, false);
        kirin_hypha_set_attack_enabled (hyphaHandle, false);
        analysisApplication.applicationFailed();
    }
    return accepted;
}

bool KirinHyphaProcessorBase::setAnalysisDemand (
    std::uint64_t owner, hypha::analysis::Demand demand)
{
    if (role != Role::Post || owner == 0
        || ! analysisDemandOwner.isCurrent (owner)
        || ! hypha::analysis::valid (demand)
        || (hypha::analysis::isAttack (demand)
            && ! hypha::meter_context::drumAttackAvailable (meterContextPreference()))
        || ((demand.kind == hypha::analysis::Kind::midSideSpectrum
             || demand.channelMode == KIRIN_SPECTRUM_CHANNEL_SIDE)
            && getTotalNumInputChannels() != 2))
        return false;

    const juce::ScopedLock sl (handleLock);
    const auto changed = analysisDemandOwner.requested() != demand;
    if (! analysisDemandOwner.set (owner, demand))
        return false;
    if (changed)
        analysisApplication.requestChanged();
    // The request is accepted even while a new handle is preparing. Its one authoritative
    // application point runs after enable, and visible-editor service retries bounded failures.
    serviceRequestedAnalysisUnderHandleLock();
    return true;
}

void KirinHyphaProcessorBase::releaseAnalysisDemand (std::uint64_t owner)
{
    if (role != Role::Post || owner == 0
        || ! analysisDemandOwner.isCurrent (owner))
        return;
    const juce::ScopedLock sl (handleLock);
    const auto changed = hypha::analysis::active (analysisDemandOwner.requested());
    if (! analysisDemandOwner.clear (owner))
        return;
    if (changed)
        analysisApplication.requestChanged();
    serviceRequestedAnalysisUnderHandleLock();
}

void KirinHyphaProcessorBase::endAnalysisUiSession (std::uint64_t owner)
{
    if (role != Role::Post || owner == 0)
        return;
    const juce::ScopedLock sl (handleLock);
    if (! analysisDemandOwner.isCurrent (owner))
        return;
    analysisDemandOwner.end (owner);
    analysisApplication.requestChanged();
    serviceRequestedAnalysisUnderHandleLock();
}

bool KirinHyphaProcessorBase::pollAttackBatch (KirinAttackBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_attack_batch (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollAttackEvents (KirinAttackEventBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_attack_events (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollAttackWaveform (KirinAttackWaveformBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_attack_waveform (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollAttackDetails (KirinAttackDetailBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_attack_details (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollAttackPreWaveform (KirinAttackWaveformBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_attack_pre_waveform (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollAttackPreDetails (KirinAttackDetailBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_attack_pre_details (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollAttackPairEvents (KirinAttackPairEventBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_attack_pair_events (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::attackStats (KirinAttackStats& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_attack_stats (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollPsb (KirinPsbView& out) const
{
    const juce::ScopedLock sl (handleLock);
    return role == Role::Post && hyphaHandle != nullptr
        && kirin_hypha_poll_psb (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::setSpectrumChannelMode (uint8_t channelMode)
{
    if (role != Role::Post || channelMode > KIRIN_SPECTRUM_CHANNEL_SIDE)
        return false;
    if (channelMode == KIRIN_SPECTRUM_CHANNEL_SIDE
        && getTotalNumInputChannels() != 2)
        return false;
    preferredSpectrumChannelMode.store (channelMode, std::memory_order_release);
    return true;
}

bool KirinHyphaProcessorBase::setSpectrumDisplaySelection (
    uint8_t selection, bool absoluteTarget)
{
    if (role != Role::Post || selection > KIRIN_SPECTRUM_SELECTION_MID_SIDE)
        return false;
    const bool midSide = selection == KIRIN_SPECTRUM_SELECTION_MID_SIDE;
    const bool stereoOnly = selection == KIRIN_SPECTRUM_CHANNEL_SIDE || midSide;
    if ((stereoOnly && getTotalNumInputChannels() != 2)
        || (midSide && ! absoluteTarget))
        return false;
    if (! midSide)
        preferredSpectrumChannelMode.store (selection, std::memory_order_release);
    preferredSpectrumDisplaySelection.store (selection, std::memory_order_release);
    return true;
}

bool KirinHyphaProcessorBase::pollSpectrum (KirinSpectrumView& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_spectrum (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollSpectrumBatch (KirinSpectrumBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_spectrum_batch (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollMidSideSpectrum (KirinMidSideSpectrumView& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_mid_side_spectrum (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollPerceptual (KirinPerceptualView& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_perceptual (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollPerceptualBatch (KirinPerceptualBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_perceptual_batch (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollAbsoluteBatch (KirinAbsoluteBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_absolute_batch (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollAnalysisOwnerNames (juce::String& out) const
{
    if (role != Role::Post)
        return false;
    KirinAnalysisOwners owners {};
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr || ! kirin_hypha_poll_analysis_owners (hyphaHandle, &owners))
        return false;
    if (owners.count != KIRIN_ANALYSIS_SLOT_COUNT)
    {
        out.clear();
        return true;
    }
    juce::StringArray names;
    for (size_t index = 0u; index < KIRIN_ANALYSIS_SLOT_COUNT; ++index)
    {
        const auto* utf8 = owners.names[index];
        if (utf8[0] == '\0')
        {
            out.clear();
            return true;
        }
        names.add (juce::String (juce::CharPointer_UTF8 (utf8)));
    }
    out = names.joinIntoString (", ");
    return true;
}
