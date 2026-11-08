#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceCopyCorrespondence.h"
#include "ReferenceLiveInputRestore.h"

namespace hypha::reference_audition
{
   #if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    void observeReferenceNormalPathRead (const RuntimeV2Controller*);
   #endif
    void RuntimeV2Controller::serviceDeferredAudioThreadActions()
    {
        // Journal observes the completed session before this worker retires its data.
        ReferenceSessionRetirement retirement;
        if (blind.completeNormalReturn (retirement)) releaseOutputGate (retirement.outputGateToken);
        if (auditionReturnPending.exchange (false, std::memory_order_acq_rel) && ! hasHeldSelection())
            requestAuditionReturnEvent (aAudibleConfirmations.load (std::memory_order_acquire));
        const auto pending = blindGateReleasePendingToken.exchange (0, std::memory_order_acq_rel);
        ReferenceSessionRetirement cancelled;
        if (pending && blind.cancelUnheardStart (cancelled) && cancelled.outputGateToken == pending)
            releaseOutputGate (pending);
    }

    // The new song has taken this block's path. An audible previous song crossfades directly into it
    // from a private copy of its last path, and its page bank stays open until the blend ends.
    void RuntimeV2Controller::beginSwitchBlend (const NormalPath& path) noexcept
    {
        if (rtPath.valid && rtNormalBlend > 0.0f)
        {
            rtOutgoing = rtPath;
            rtOutgoing.gain = rtTrackingRamp.value();
            rtSwitchBlend = 0.0f;
            rtSwitchBlending = true;
        }
        else normalSwitchRetired.store (path.switchSerial, std::memory_order_release);
        rtTrackingRamp.settle (path.gain);
    }

    void RuntimeV2Controller::finishSwitchBlend() noexcept
    {
        if (! rtSwitchBlending) return;
        rtSwitchBlending = false;
        normalSwitchRetired.store (rtPath.switchSerial, std::memory_order_release);
    }

    bool RuntimeV2Controller::renderOutgoingSong (std::int64_t hostPosition, int channels, int frames) noexcept
    {
        float* const channelData[2] { (*normalOutgoing)[0].data(), (*normalOutgoing)[1].data() };
        juce::AudioBuffer<float> outgoing (channelData, channels, frames);
        auto position = rtOutgoing.position (hostPosition, versionComparison);
        const bool restart = rtOutgoing.restarts (versionComparison);
        if (position < 0 && restart) position = rtOutgoing.looped (hostPosition);
        return position >= 0 && rtOutgoing.pages->renderCueResult (outgoing, position, rtOutgoing.start, rtOutgoing.end,
            rtOutgoing.loops || restart, rtOutgoing.gain, rtOutgoing.pageGeneration) == AudioPages::RenderResult::rendered;
    }

    void RuntimeV2Controller::serviceOutputRetirement()
    {
        if (versionComparison && !latestPlaying.load (std::memory_order_acquire)) blind.confirmStoppedReturn();
        releaseOutputGate (blind.retirableOutputGateToken());
        if (normalReturnToken.load (std::memory_order_acquire) && !latestPlaying.load (std::memory_order_acquire))
            releaseOutputGate (normalReturnToken.exchange (0, std::memory_order_acq_rel));
        releaseOutputGate (normalGateReleasePendingToken.exchange (0, std::memory_order_acq_rel));
    }

    bool RuntimeV2Controller::renderSelectedB (juce::AudioBuffer<float>& buffer,
                                               std::int64_t hostPosition,
                                               bool positionValid,
                                               bool auditionAllowed) noexcept
    {
        return renderSelectedB (buffer, hostPosition, positionValid,
                                auditionAllowed, auditionAllowed);
    }

    bool RuntimeV2Controller::renderSelectedB (juce::AudioBuffer<float>& buffer,
                                               std::int64_t hostPosition,
                                               bool positionValid,
                                               bool auditionAllowed,
                                               bool normalReturnAllowed, bool normalTarget) noexcept
    {
        const bool activeTransport = auditionAllowed
                                  && latestPlaying.load (std::memory_order_acquire)
                                  && positionValid;
        const bool normalReturnTransport = normalReturnAllowed
                                        && latestPlaying.load (std::memory_order_acquire)
                                        && positionValid;
        if (blind.auditioning()
            && ! blind.matchesAuditionEpoch (
                auditionEpoch.load (std::memory_order_acquire)))
            invalidateBlindFromAudioThread();
        if (blind.renderInvalidatedA (buffer, normalReturnTransport))
            return true;
        if (blind.ongoing())
        {
            if (rtNormalBlend > 0.0f)
            { if (rtNormalEpoch == auditionEpoch.load (std::memory_order_acquire)) blind.seedNormalSourceBlend (rtNormalBlend);
              rtNormalBlend = 0.0f; normalAudible.store (false); }
            if (! blind.auditioning())
                return false;
            if (versionComparison && auditionAllowed
                && !latestPlaying.load (std::memory_order_acquire) && ready.load (std::memory_order_acquire))
                return blind.renderPausedA (buffer);
            if (! activeTransport || ! ready.load (std::memory_order_acquire)
                || ! blind.render (buffer, hostPosition, positionValid, &currentPages(), mappedSourcePosition (hostPosition)))
            {
                invalidateBlindFromAudioThread();
                return blind.renderInvalidatedA (buffer, normalReturnTransport);
            }
            return true;
        }
        if (! auditionAllowed || ! latestPlaying.load (std::memory_order_acquire)
            || ! positionValid || (! bSelected.load (std::memory_order_acquire) && !normalReturnToken.load (std::memory_order_acquire) && !normalAudible.load (std::memory_order_acquire))
            || ! ready.load (std::memory_order_acquire)
            || activeAuditionEpoch.load (std::memory_order_acquire)
                   != auditionEpoch.load (std::memory_order_acquire))
        {
            if (bSelected.load (std::memory_order_acquire) || normalReturnToken.load (std::memory_order_acquire))
                failClosedToAFromAudioThread();
            rtNormalBlend = 0.0f; normalAudible.store (false, std::memory_order_release);
            return false;
        }
        const int frames = buffer.getNumSamples(), channels = buffer.getNumChannels();
        if (frames < 1 || frames > 8192 || channels < 1 || channels > 2)
        { failClosedToAFromAudioThread(); rtNormalBlend = 0.0f; normalAudible.store (false); return false; }
        const auto epoch = activeAuditionEpoch.load (std::memory_order_acquire);
        if (rtNormalEpoch != epoch) { rtNormalBlend = 0.0f; rtNormalEpoch = epoch; finishSwitchBlend(); rtPath = {}; }
        if (rtNormalBlend <= 0.0f) finishSwitchBlend();
        const auto gateToken = activeOutputGateToken.load (std::memory_order_acquire);
        const bool selected = normalTarget && bSelected.load (std::memory_order_acquire);
        const int rampFrames = trackingRampFrames.load (std::memory_order_acquire);
        for (int c = 0; c < channels; ++c)
            std::copy_n (buffer.getReadPointer (c), frames, normalLiveA[static_cast<size_t> (c)].data());
        const auto inputContinuity = aCapture.continuity();
        NormalPath path;
        bool rendered = false;
        auto pageResult = AudioPages::RenderResult::invalid;
        for (int attempt = 0; attempt < 3 && ! rendered; ++attempt)
        {
            NormalPath read;
            // A paired song switch is being written: this block belongs to the last complete path.
            if (! readNormalPath (read))
            {
                if (! rtPath.valid || rtNormalBlend <= 0.0f) continue;
                read = rtPath;
            }
           #if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
            observeReferenceNormalPathRead (this);
           #endif
            auto sourcePosition = read.position (hostPosition, versionComparison);
            // Cue の頭から鳴らし直す役は、Cue の終わり・DAW のループで外へ出ても止めずに周回する（A へ切ると約 0.5 秒
            // 鳴らず、Rust の試聴と記録を作り直す）。
            const bool restart = read.restarts (versionComparison);
            if (sourcePosition < 0 && restart)
                sourcePosition = read.looped (hostPosition);
            if (sourcePosition < 0)
                continue;
            path = read;
            // Only the PCM opened with this path: a callback that stalled while its bank was reused for another
            // song reads nothing and takes the current path, never the other song at this song's gain.
            pageResult = path.pages->renderCueResult (buffer, sourcePosition, path.start, path.end, path.loops || restart, 1.0f,
                                                      path.pageGeneration);
            rendered = pageResult == AudioPages::RenderResult::rendered;
        }
        // A paired switch leaves the old bank open until this thread retires it, so a complete block
        // of the previous song stays valid. Any other replacement of the bank or the map ends the block.
        const bool pairedSwitch = rendered && (normalSwitchWriting.load (std::memory_order_acquire)
            || path.switchSerial != normalSwitchSerial.load (std::memory_order_acquire));
        // A completed preparation can replace the page bank while an earlier callback
        // is finishing. That callback must never mix the new PCM with the old gain.
        if (epoch != auditionEpoch.load (std::memory_order_acquire)
            || epoch != activeAuditionEpoch.load (std::memory_order_acquire)
            || (! pairedSwitch && path.valid && path.pages != &currentPages())
            || (rendered && ! pairedSwitch && path.mapping != mappingGeneration.load (std::memory_order_acquire)))
        {
            for (int c = 0; c < channels; ++c)
                std::copy_n (normalLiveA[static_cast<size_t> (c)].data(), frames, buffer.getWritePointer (c));
            rtNormalBlend = 0.0f; normalAudible.store (false, std::memory_order_release);
            finishSwitchBlend();
            return false;
        }
        if (! rendered)
        {
            restoreReferenceLiveInput (buffer, [&] (int c) { return normalLiveA[static_cast<size_t> (c)].data(); });
            rtNormalBlend = 0.0f; normalAudible.store (false, std::memory_order_release);
            finishSwitchBlend();
            if (pageResult == AudioPages::RenderResult::waitingForPages
                && path.mapping == mappingGeneration.load (std::memory_order_acquire))
            {
                normalPageWaiting.store (true, std::memory_order_release);
                if (!selected)
                {
                    auto returningToken = gateToken;
                    if (returningToken && normalReturnToken.compare_exchange_strong (returningToken, 0, std::memory_order_acq_rel))
                        normalGateReleasePendingToken.store (gateToken, std::memory_order_release);
                }
            }
            else failClosedToAFromAudioThread();
            return false;
        }
        normalPageWaiting.store (false, std::memory_order_release);
        const bool verifyCopy = versionComparison && copyCorrespondence.load (std::memory_order_acquire);
        bool pcmAgrees = false;
        if (verifyCopy
            && ! referenceCopyCorresponds (static_cast<size_t> (frames * channels),
                [&] (size_t i) { return normalLiveA[i % static_cast<size_t> (channels)][i / static_cast<size_t> (channels)]; },
                [&] (size_t i) { return buffer.getSample (static_cast<int> (i % static_cast<size_t> (channels)),
                                                         static_cast<int> (i / static_cast<size_t> (channels))); },
                contentMappingContinuity.load (std::memory_order_acquire) != inputContinuity, &pcmAgrees))
        {
            for (int c = 0; c < channels; ++c)
                std::copy_n (normalLiveA[static_cast<size_t> (c)].data(), frames, buffer.getWritePointer (c));
            // Retire a trusted map once. Rejected candidate blocks must not keep
            // discarding the continuous A needed to establish a different offset.
            if (! pcmAgrees && (normalAudible.load (std::memory_order_acquire)
                || contentMappingContinuity.load (std::memory_order_acquire) == inputContinuity))
                aCapture.invalidateObservation();
            failClosedToAFromAudioThread(); rtNormalBlend = 0.0f; normalAudible.store (false);
            return false;
        }
        const float targetGain = path.gain;
        if (path.switchSerial != rtPath.switchSerial) beginSwitchBlend (path);
        rtPath = path;
        // gain が変わったら 50 ms の直線で動かす。まだ聴こえていない（入りのフェードの前）なら即座に合わせる。
        if (rtNormalBlend <= 0.0f) rtTrackingRamp.settle (targetGain);
        if (rtSwitchBlending && ! renderOutgoingSong (hostPosition, channels, frames))
        {
            // The previous song cannot finish this crossfade, so the new one enters from A.
            rtNormalBlend = 0.0f;
            finishSwitchBlend();
        }
        const float step = normalFadeStep.load (std::memory_order_acquire);
        for (int f = 0; f < frames; ++f)
        {
            const float gain = rtTrackingRamp.next (targetGain, rampFrames);
            rtNormalBlend = selected ? juce::jmin (1.0f, rtNormalBlend + step) : juce::jmax (0.0f, rtNormalBlend - step);
            if (rtSwitchBlending) rtSwitchBlend = juce::jmin (1.0f, rtSwitchBlend + step);
            for (int c = 0; c < channels; ++c)
            {
                const auto a = normalLiveA[static_cast<size_t> (c)][static_cast<size_t> (f)];
                auto b = buffer.getSample (c, f) * gain;
                if (rtSwitchBlending)
                {
                    const auto previous = (*normalOutgoing)[static_cast<size_t> (c)][static_cast<size_t> (f)];
                    b = previous + (b - previous) * rtSwitchBlend;
                }
                buffer.setSample (c, f, rtNormalBlend == 0.0f ? a : rtNormalBlend == 1.0f ? b : a + (b - a) * rtNormalBlend);
            }
        }
        if (rtSwitchBlending && rtSwitchBlend >= 1.0f) finishSwitchBlend();
        normalAudible.store (rtNormalBlend > 0.0f, std::memory_order_release);
        if (!selected && rtNormalBlend == 0.0f)
        {
            // A new selection owns a new token; an older callback cannot consume its return.
            auto returningToken = gateToken;
            if (returningToken != 0 && normalReturnToken.compare_exchange_strong (returningToken, 0, std::memory_order_acq_rel))
                normalGateReleasePendingToken.store (gateToken, std::memory_order_release);
        }
        if (selected) bAudibleConfirmations.fetch_add (1, std::memory_order_release);
        return true;
    }
}
