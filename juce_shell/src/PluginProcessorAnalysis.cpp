#include "PluginProcessor.h"

namespace
{
struct AnalysisFfiAdapter
{
    KirinHypha* handle = nullptr;
    bool setSpectrumVisible (bool value) const
    { return kirin_hypha_set_spectrum_visible (handle, value); }
    bool setAttackEnabled (bool value) const
    { return kirin_hypha_set_attack_enabled (handle, value); }
    bool setChannelMode (std::uint8_t value) const
    { return kirin_hypha_set_spectrum_channel_mode (handle, value); }
    bool setMidSideSpectrumVisible (bool value) const
    { return kirin_hypha_set_mid_side_spectrum_visible (handle, value); }
    bool setAbsoluteVisible (bool value) const
    { return kirin_hypha_set_absolute_visible (handle, value); }
    bool setPerceptualVisible (bool value) const
    { return kirin_hypha_set_perceptual_visible (handle, value); }
};
}

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
    }
    else if (hypha::analysis::usesChannelMode (demand.kind)
             && demand.channelMode == KIRIN_SPECTRUM_CHANNEL_SIDE)
    {
        demand.channelMode = KIRIN_SPECTRUM_CHANNEL_LR;
        analysisDemandOwner.replaceCurrentRequest (demand);
    }
}

std::uint64_t KirinHyphaProcessorBase::beginAnalysisUiSession() noexcept
{
    if (role != Role::Post)
        return 0;
    const juce::ScopedLock sl (handleLock);
    const auto owner = analysisDemandOwner.begin();
    if (hyphaHandle != nullptr && writesEnabled.load (std::memory_order_acquire))
        applyAnalysisDemandUnderHandleLock ({});
    else
        analysisDemandApplied = {};
    return owner;
}

bool KirinHyphaProcessorBase::applyAnalysisDemandUnderHandleLock (
    hypha::analysis::Demand demand)
{
    if (hyphaHandle == nullptr || ! writesEnabled.load (std::memory_order_acquire))
        return false;
    if (analysisDemandApplied == demand)
        return true;

    AnalysisFfiAdapter adapter { hyphaHandle };
    const bool accepted = hypha::analysis::apply (analysisDemandApplied, demand, adapter);

    if (accepted)
        analysisDemandApplied = demand;
    else
    {
        kirin_hypha_set_spectrum_visible (hyphaHandle, false);
        kirin_hypha_set_attack_enabled (hyphaHandle, false);
        analysisDemandApplied = {};
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
    if (! analysisDemandOwner.set (owner, demand))
        return false;
    return applyAnalysisDemandUnderHandleLock (demand);
}

void KirinHyphaProcessorBase::releaseAnalysisDemand (std::uint64_t owner)
{
    if (role != Role::Post || owner == 0
        || ! analysisDemandOwner.isCurrent (owner))
        return;
    const juce::ScopedLock sl (handleLock);
    if (! analysisDemandOwner.clear (owner))
        return;
    const auto none = hypha::analysis::Demand {};
    if (hyphaHandle != nullptr && writesEnabled.load (std::memory_order_acquire))
        applyAnalysisDemandUnderHandleLock (none);
    else
        analysisDemandApplied = none;
}

void KirinHyphaProcessorBase::endAnalysisUiSession (std::uint64_t owner)
{
    if (role != Role::Post || owner == 0)
        return;
    const juce::ScopedLock sl (handleLock);
    if (! analysisDemandOwner.isCurrent (owner))
        return;
    const auto none = hypha::analysis::Demand {};
    if (hyphaHandle != nullptr && writesEnabled.load (std::memory_order_acquire))
        applyAnalysisDemandUnderHandleLock (none);
    else
        analysisDemandApplied = none;
    analysisDemandOwner.end (owner);
}

void KirinHyphaProcessorBase::restoreRequestedAnalysisUnderHandleLock()
{
    analysisDemandApplied = {};
    const auto demand = requestedAnalysisDemand();
    if (hypha::analysis::active (demand))
        applyAnalysisDemandUnderHandleLock (demand);
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
