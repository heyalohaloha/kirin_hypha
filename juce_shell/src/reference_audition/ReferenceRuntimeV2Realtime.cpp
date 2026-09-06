#include "ReferenceRuntimeV2Controller.h"

namespace hypha::reference_audition
{
    void RuntimeV2Controller::serviceDeferredAudioThreadActions()
    {
        if (auditionReturnPending.exchange (false, std::memory_order_acq_rel))
            requestAuditionReturnEvent (aAudibleConfirmations.load (std::memory_order_acquire));
        if (gateReleasePending.exchange (false, std::memory_order_acq_rel) && selectionGate)
            selectionGate (false);
    }

    bool RuntimeV2Controller::renderSelectedB (juce::AudioBuffer<float>& buffer,
                                               std::int64_t hostPosition,
                                               bool positionValid,
                                               bool auditionAllowed) noexcept
    {
        const bool activeTransport = auditionAllowed
                                  && latestPlaying.load (std::memory_order_acquire)
                                  && positionValid;
        if (blind.renderInvalidatedA (buffer, activeTransport))
            return true;
        if (blind.ongoing())
        {
            if (! activeTransport || ! ready.load (std::memory_order_acquire)
                || ! blind.render (buffer, hostPosition, positionValid))
            {
                invalidateBlindFromAudioThread();
                return blind.renderInvalidatedA (buffer, activeTransport);
            }
            return true;
        }
        if (! auditionAllowed || ! latestPlaying.load (std::memory_order_acquire)
            || ! positionValid || ! bSelected.load (std::memory_order_acquire)
            || ! ready.load (std::memory_order_acquire))
        {
            if (bSelected.load (std::memory_order_acquire))
                failClosedToAFromAudioThread();
            return false;
        }
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
            failClosedToAFromAudioThread();
            return false;
        }
        bAudibleConfirmations.fetch_add (1, std::memory_order_release);
        return true;
    }
}
