#include "PluginProcessor.h"

KirinHyphaProcessorBase::~KirinHyphaProcessorBase()
{
    cancelPendingUpdate();
    stopTimer(); // Stop the non-RT enable poll before teardown.
    localBlindCapture.stop();
#if ! KIRIN_HYPHA_PRE_DISPLAY
    referenceAuditionController.reset();
#endif
#if KIRIN_HYPHA_GUIDE_TRANSPORT
    preDisplayController.reset();
#endif
    const juce::ScopedLock sl (handleLock);
    // The host has quiesced callbacks for destruction. Retire the C++ PRE mapping before the
    // Rust engine releases its identity claim, so a new owner can create the same ring at once.
    stopLiveCompareForFormatChange();
    // A format change keeps POST's drain receipt and exclusion; final destruction has no future callback.
    if (liveCompare.candidate.scope != 0) releaseLocalBlindProductScope (liveCompare.candidate.scope);
    liveCompare.ring.retire();
    const bool collected = liveCompare.ring.collect();
    jassert (collected && ! liveCompare.ring.hasStorage());
    juce::ignoreUnused (collected);
    preparedProductMode.store (static_cast<uint8_t> (kirin::PreparedProductMode::none), std::memory_order_release);
    if (hyphaHandle != nullptr)
    {
        if (role == Role::Post)
        {
            kirin_hypha_set_spectrum_visible (hyphaHandle, false);
            kirin_hypha_set_attack_enabled (hyphaHandle, false);
        }
        kirin_hypha_destroy (hyphaHandle);
        hyphaHandle = nullptr;
        analysisApplication.engineDestroyed();
    }
}
