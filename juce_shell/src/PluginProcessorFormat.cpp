#include "PluginProcessor.h"
#include "ChannelRoles.h"
#include "PluginEditor.h"

// Format binding: which audio format the Rust engine is built for, and what happens when the host
// negotiates a different one. Split out of PluginProcessor.cpp in B-961, when an incompatible
// re-prepare during Record stopped being forgotten (棚卸し §16.4). The decision itself lives in
// PreparedFormat.h so it can be tested without a host.

namespace
{
    // Logic stopped-state fix: expose Inactive PRE/POST presence without waiting for the first audio callback.
    // The 50 ms Timer grants a bounded state-restore window before enabling from prepareToPlay.
    constexpr int kPrepareEnableDelayTicks = 10;
    // B-125 (b): prealloc-max headroom (frames). The interleave scratch is sized in
    // prepareToPlay to max(maximumExpectedSamplesPerBlock, this) frames so that realistic
    // variable / offline-render blocks larger than the realtime-declared block are still
    // measured without a (non-RT-safe) reallocation on the audio thread. Hosts can deliver
    // offline / freeze / bounce blocks well above the realtime maximum; 262144 frames
    // (~5.46 s @ 48 kHz) absorbs large offline chunks while keeping the one-time, non-RT
    // prepareToPlay allocation bounded at 262144 * numCh * sizeof(float) (≈2 MB stereo).
    // Pathological blocks beyond this ceiling are not reallocated; their frames are counted as
    // oversized drops (B-125 (c) / kirin_hypha_note_oversized_drop) while audio passes through.
    constexpr int kOversizeHeadroomFrames = 262144;
} // namespace

void KirinHyphaProcessorBase::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = getTotalNumInputChannels();
    // Pre-allocate the interleave scratch so processBlock never allocates (RT-safe).
    // B-125 (b): prealloc-max — size to max(declared block, kOversizeHeadroomFrames) frames
    // so realistic variable / offline-render blocks above the realtime maximum are absorbed
    // without an audio-thread realloc. This .assign runs in prepareToPlay (non-RT) — allowed.
    const int   maxFrames = juce::jmax (juce::jmax (0, samplesPerBlock), kOversizeHeadroomFrames);
    interleaveScratch.assign ((size_t) maxFrames * (size_t) juce::jmax (1, numCh), 0.0f);
    // B-125: cache the prepared capacity so processBlock re-checks against it (the oversized
    // fallback fires only for blocks beyond this) without re-deriving from samplesPerBlock.
    scratchCapacitySamples = interleaveScratch.size();
    const auto roles = kirin::channelRoles (getChannelLayoutOfBus (true, 0));
    stopLocalBlindCaptureForFormatChange (sampleRate, roles);
    const juce::ScopedLock sl (handleLock);
    normalizeSpectrumSelectionForInputChannels (numCh);
    // B-141: Studio One offline bounce re-prepares after All Keep entered Record. The block size
    // may change for render, but the user-visible Record state must not be thrown away, so the
    // same format reuses the engine. B-334: an incompatible one is not Stop authority while Record
    // is armed. B-961: it is held rather than dropped — see PreparedFormat.h.
    switch (kirin::decidePrepare (hyphaHandle != nullptr,
                                  hyphaHandle != nullptr && kirin_hypha_is_recording (hyphaHandle),
                                  sampleRate, roles, preparedFormat))
    {
        case kirin::PrepareAction::reuse:
            return;
        case kirin::PrepareAction::holdForRecord:
            heldFormat.hold (sampleRate, samplesPerBlock);
            formatChangeHeld.store (true, std::memory_order_release);
            startTimer (50); // the held format is applied the moment Record lets go of the engine
            return;
        case kirin::PrepareAction::rebuild:
            break;
    }

    selectReferenceA(); // A is mandatory before replacing the comparison-suspension owner.

    lastProcessPositionValid = false;
    lastProcessHadPosition = false;
    lastProcessNumFrames = 0;
    watchSilenceGate.reset();
    writesEnabled.store (false, std::memory_order_release);
    analysisApplication.engineDestroyed();
    if (hyphaHandle != nullptr)
    {
        kirin_hypha_destroy (hyphaHandle);
        hyphaHandle = nullptr;
    }
    hyphaHandle = kirin::createEngineForRoles (sampleRate, roles);
    if (hyphaHandle != nullptr) analysisApplication.engineCreated();
    preparedFormat = hyphaHandle != nullptr ? kirin::PreparedFormat { sampleRate, roles }
                                            : kirin::PreparedFormat {};
    heldFormat.release();
    formatChangeHeld.store (false, std::memory_order_release);

    // A fresh handle receives the current entitlement immediately. Further refreshes are tied to
    // editor open / explicit Keep / pair-menu actions; there is no steady-state disk polling.
    // set_identity + enable_*_writes are deferred to the message-thread Timer
    // (enableWritesNow) so any setStateInformation restore is applied before enable.
    if (hyphaHandle != nullptr)
    {
        const uint8_t lic = kirin_hypha_load_license();
        cachedLicenseCode.store ((int) lic, std::memory_order_release);
        kirin_hypha_set_license (hyphaHandle, lic);
        // A recalled Studio Pro session may deliver setActive(false) before prepareToPlay creates
        // the Rust engine. Apply the retained host fact to every fresh handle so an insert that
        // was already OFF at project-open reaches the same ABS state as an explicit live click.
        kirin_hypha_set_host_component_active (hyphaHandle, hostComponentActive);
        // Logic stopped-state fix: re-prepare needs a fresh enable, but Logic may not call processBlock until
        // playback. Start a message-thread fallback so Inactive presence/candidates are published
        // even while stopped. If setStateInformation already arrived for this instance, skip the
        // grace delay; otherwise keep the window so project recall can restore identity before the
        // io_thread snapshots it.
        const int restoreDelay = stateInformationSeen.load (std::memory_order_acquire) ? 0 : kPrepareEnableDelayTicks;
        enableDelayTicks.store (restoreDelay, std::memory_order_release);
        enablePending.store (true, std::memory_order_release);
        startTimer (50);
    }
    // A null handle (create failure) is tolerated; processBlock / pollMeasureResult guard on it.
}

void KirinHyphaProcessorBase::applyHeldFormatIfRecordReleased()
{
    // B-334 keeps Stop authority with the user, so nothing here stops Record. It only re-applies
    // the format the host asked for once Record has released the engine on its own.
    double sampleRate = 0.0;
    int blockFrames = 0;
    {
        const juce::ScopedLock sl (handleLock);
        if (! heldFormat.held || hyphaHandle == nullptr
            || kirin_hypha_is_recording (hyphaHandle))
            return;
        sampleRate = heldFormat.sampleRate;
        blockFrames = heldFormat.maxBlockFrames;
    }
    // prepareToPlay reallocates interleaveScratch and replaces hyphaHandle. processBlock reads
    // both WITHOUT handleLock, on the documented contract that the host suspends processing
    // around prepareToPlay (PluginProcessor.cpp: "Not locked on the audio thread"). A host-driven
    // call honours that contract; this one is ours, from the message thread, and does not — so it
    // takes the lock JUCE provides for exactly this (AudioProcessor::getCallbackLock), which
    // blocks the audio callback for the duration instead of letting it read freed memory.
    //
    // suspendProcessing() is NOT the alternative here: it makes the host emit an empty buffer,
    // which would mute the user's audio. Blocking briefly keeps A passing (R-12).
    const juce::ScopedLock audioCallback (getCallbackLock());
    prepareToPlay (sampleRate, blockFrames);
}
