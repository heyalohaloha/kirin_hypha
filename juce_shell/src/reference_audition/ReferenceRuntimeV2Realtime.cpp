#include "ReferenceRuntimeV2Controller.h"

namespace hypha::reference_audition
{
    void RuntimeV2Controller::serviceDeferredAudioThreadActions()
    {
        // Journal observes the completed session before this worker retires its data.
        ReferenceSessionRetirement retirement;
        if (blind.completeNormalReturn (retirement)) releaseOutputGate (retirement.outputGateToken);
        if (auditionReturnPending.exchange (false, std::memory_order_acq_rel))
            requestAuditionReturnEvent (aAudibleConfirmations.load (std::memory_order_acquire));
        const auto pending = blindGateReleasePendingToken.exchange (0, std::memory_order_acq_rel);
        ReferenceSessionRetirement cancelled;
        if (pending && blind.cancelUnheardStart (cancelled) && cancelled.outputGateToken == pending)
            releaseOutputGate (pending);
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
                || ! blind.render (buffer, hostPosition, positionValid, &pages, mappedSourcePosition (hostPosition)))
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
        if (rtNormalEpoch != epoch) { rtNormalBlend = 0.0f; rtNormalEpoch = epoch; }
        const auto gateToken = activeOutputGateToken.load (std::memory_order_acquire);
        const bool selected = normalTarget && bSelected.load (std::memory_order_acquire);
        for (int c = 0; c < channels; ++c)
            std::copy_n (buffer.getReadPointer (c), frames, normalLiveA[static_cast<size_t> (c)].data());
        bool rendered = false;
        for (int attempt = 0; attempt < 3 && ! rendered; ++attempt)
        {
            const auto generation = mappingGeneration.load (std::memory_order_acquire);
            if ((generation & 1u) != 0)
                continue;
            const auto sourcePosition = mappedSourcePosition (hostPosition);
            const auto start = cueStart.load (std::memory_order_relaxed);
            const auto end = cueEnd.load (std::memory_order_relaxed);
            const auto loops = cueLoops.load (std::memory_order_relaxed);
            if (sourcePosition < 0
                || mappingGeneration.load (std::memory_order_acquire) != generation)
                continue;
            rendered = pages.renderCue (
                buffer, sourcePosition, start, end, loops,
                bLinearGain.load (std::memory_order_acquire));
        }
        if (! rendered)
        {
            failClosedToAFromAudioThread(); rtNormalBlend = 0.0f; normalAudible.store (false);
            return false;
        }
        const float step = normalFadeStep.load (std::memory_order_acquire);
        for (int f = 0; f < frames; ++f)
        {
            rtNormalBlend = selected ? juce::jmin (1.0f, rtNormalBlend + step) : juce::jmax (0.0f, rtNormalBlend - step);
            for (int c = 0; c < channels; ++c)
            {
                const auto a = normalLiveA[static_cast<size_t> (c)][static_cast<size_t> (f)];
                const auto b = buffer.getSample (c, f);
                buffer.setSample (c, f, rtNormalBlend == 0.0f ? a : rtNormalBlend == 1.0f ? b : a + (b - a) * rtNormalBlend);
            }
        }
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
