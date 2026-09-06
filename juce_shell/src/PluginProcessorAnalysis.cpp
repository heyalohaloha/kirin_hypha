#include "PluginProcessor.h"

bool KirinHyphaProcessorBase::setPsbVisible (bool delta)
{
    if (role != Role::Post) return false;
    // Retain the single Analysis lease and restore its LR definition after engine recreation.
    // Neither this choice nor its fixed LR source changes the saved Spectrum/SHARP mode.
    psbAnalysisRequested.store (true);
    perceptualAnalysisRequested.store (delta);
    absoluteAnalysisRequested.store (! delta);
    attackRequested.store (false);
    spectrumVisibleRequested.store (true);
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr || ! writesEnabled.load()) return false;
    if (! kirin_hypha_set_spectrum_channel_mode (hyphaHandle, KIRIN_SPECTRUM_CHANNEL_LR)) return false;
    return delta ? kirin_hypha_set_perceptual_visible (hyphaHandle, true)
                 : kirin_hypha_set_absolute_visible (hyphaHandle, true);
}

bool KirinHyphaProcessorBase::pollPsb (KirinPsbView& out) const
{
    const juce::ScopedLock sl (handleLock);
    return role == Role::Post && hyphaHandle != nullptr
        && kirin_hypha_poll_psb (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::setSpectrumVisible (bool visible)
{
    psbAnalysisRequested.store (false);
    if (role != Role::Post)
        return false;
    if (visible)
    {
        perceptualAnalysisRequested.store (false, std::memory_order_release);
        absoluteAnalysisRequested.store (false, std::memory_order_release);
        attackRequested.store (false, std::memory_order_release);
    }
    spectrumVisibleRequested.store (visible, std::memory_order_release);
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr || ! writesEnabled.load (std::memory_order_acquire))
        return false;
    if (visible && ! kirin_hypha_set_spectrum_channel_mode (
            hyphaHandle,
            preferredSpectrumChannelMode.load (std::memory_order_acquire)))
        return false;
    return kirin_hypha_set_spectrum_visible (hyphaHandle, visible);
}

bool KirinHyphaProcessorBase::setPerceptualVisible (bool visible)
{
    psbAnalysisRequested.store (false);
    if (role != Role::Post)
        return false;
    if (visible)
    {
        perceptualAnalysisRequested.store (true, std::memory_order_release);
        absoluteAnalysisRequested.store (false, std::memory_order_release);
        attackRequested.store (false, std::memory_order_release);
    }
    else
    {
        perceptualAnalysisRequested.store (false, std::memory_order_release);
    }
    spectrumVisibleRequested.store (visible, std::memory_order_release);
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr || ! writesEnabled.load (std::memory_order_acquire))
        return false;
    if (visible && ! kirin_hypha_set_spectrum_channel_mode (
            hyphaHandle,
            preferredSpectrumChannelMode.load (std::memory_order_acquire)))
        return false;
    return kirin_hypha_set_perceptual_visible (hyphaHandle, visible);
}

bool KirinHyphaProcessorBase::setAbsoluteVisible (bool visible)
{
    psbAnalysisRequested.store (false);
    if (role != Role::Post)
        return false;
    if (visible)
    {
        perceptualAnalysisRequested.store (false, std::memory_order_release);
        absoluteAnalysisRequested.store (true, std::memory_order_release);
        attackRequested.store (false, std::memory_order_release);
    }
    else
    {
        absoluteAnalysisRequested.store (false, std::memory_order_release);
    }
    spectrumVisibleRequested.store (visible, std::memory_order_release);
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr || ! writesEnabled.load (std::memory_order_acquire))
        return false;
    return kirin_hypha_set_absolute_visible (hyphaHandle, visible);
}

bool KirinHyphaProcessorBase::setSpectrumChannelMode (uint8_t channelMode)
{
    if (role != Role::Post || channelMode > KIRIN_SPECTRUM_CHANNEL_SIDE)
        return false;
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr && writesEnabled.load (std::memory_order_acquire))
    {
        if (! kirin_hypha_set_spectrum_channel_mode (hyphaHandle, channelMode))
            return false;
    }
    else if (channelMode == KIRIN_SPECTRUM_CHANNEL_SIDE
             && getTotalNumInputChannels() != 2)
    {
        return false;
    }
    preferredSpectrumChannelMode.store (channelMode, std::memory_order_release);
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
