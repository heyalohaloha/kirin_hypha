#include "ReferenceRuntimeV2Controller.h"

#include <cmath>
#include <utility>

namespace hypha::reference_audition
{
    namespace
    {
        constexpr int workerPollMs = 10;
        constexpr int workspacePolls = 50;
    }

    void RuntimeV2Controller::applyConfiguration (const Configuration& configuration)
    {
        if (blind.ongoing())
            invalidateBlind();
        else
            selectA();
        removeRuntimeFiles (activeRuntimeFiles);
        activeRuntimeFiles = {};
        revokeAuditionPublication();
        pages.close();
        aCapture.disconnect();
        workspace.reset();
        activeABinding.reset();
        activeSourceKey.clear();
        activeMappingKey.clear();
        activeContentMappingKey.clear();
        activePublishedSelectionKey.clear();
        workerSource.reset();
        sourceCache.clear();
        mappingGeneration.fetch_add (1, std::memory_order_acq_rel);
        cueStart.store (0, std::memory_order_relaxed);
        cueEnd.store (0, std::memory_order_relaxed);
        cueLoops.store (false, std::memory_order_relaxed);
        sampleLocked.store (false, std::memory_order_relaxed);
        bHostAnchor.store (0, std::memory_order_relaxed);
        bSourceAnchor.store (0, std::memory_order_relaxed);
        mappingGeneration.fetch_add (1, std::memory_order_release);
        blindContextKey.clear();
        blindPreparationKey.clear();
        activePresetAdoptionKey.clear();
        blind.clear();
        {
            const juce::ScopedLock lock (stateLock);
            publishedSource.reset();
            pendingApprovalKey.clear();
            // Audible receipts own their original immutable context across reprepare.
            // The worker completes their journal after the confirmed A return.
            blindEventSession.reset();
            pendingRecoveryRequest.reset();
            if (pendingPresetSelectionRequest)
                presetSelectionTransport.removeExchange (*pendingPresetSelectionRequest);
            pendingPresetSelectionRequest.reset();
            pendingPresetSelectionTarget.reset();
            failedPresetSelectionTarget.reset();
            if (pendingCandidatePreparationRequest)
                candidatePreparationTransport.removeExchange (*pendingCandidatePreparationRequest);
            pendingCandidatePreparationRequest.reset();
            failedCandidatePreparationTarget.reset();
            activeEventContext = {};
            activeEventCandidate = {};
            activeEventCue = {};
            activeEventSource.reset();
            currentSnapshot.recoveryStatus.clear();
            recoveryStatusExpiresAtMs = 0;
            presetSelectionWaitingSinceMs = 0;
            presetSelectionStatusExpiresAtMs = 0;
            candidatePreparationWaitingSinceMs = 0;
            candidatePreparationStatusExpiresAtMs = 0;
        }
        libraryReceived.store (false, std::memory_order_release);
        libraryOnline.store (false, std::memory_order_release);
        appliedConfigurationGeneration = configuration.generation;
        appliedSelectionGeneration = 0;
        if (! configuration.identity.valid() || ! std::isfinite (configuration.sampleRate)
            || configuration.sampleRate <= 0.0
            || (configuration.channels != 1 && configuration.channels != 2))
        {
            publish ({});
            return;
        }
        activeRuntimeFiles = configuration.identity.library ? RuntimeFiles {}
            : runtimeFiles (root, configuration.identity);
        if (activeRuntimeFiles.acknowledgement.existsAsFile())
            activeRuntimeFiles.acknowledgement.deleteFile();
        Snapshot waiting;
        waiting.state = RuntimeState::waiting;
        waiting.hostSampleRateHz = static_cast<std::int64_t> (
            std::llround (configuration.sampleRate));
        publish (std::move (waiting));
    }

    void RuntimeV2Controller::run()
    {
        int untilPoll = 0;
        auto observedTransportHeartbeat = transportHeartbeat.load (std::memory_order_acquire);
        int missedTransportCallbacks = 0;
        while (! threadShouldExit())
        {
            Configuration configuration;
            std::uint64_t selectionGeneration = 0;
            {
                const juce::ScopedLock lock (stateLock);
                configuration = requestedConfiguration;
                selectionGeneration = requestedSelection.generation;
            }
            if (configuration.generation != appliedConfigurationGeneration)
            {
                applyConfiguration (configuration);
                untilPoll = 0;
            }
            pages.service();
            aCapture.service (
                activeABinding,
                static_cast<std::int64_t> (std::llround (configuration.sampleRate)),
                configuration.channels,
                juce::Time::currentTimeMillis());
            serviceRuntimeEvents();
            serviceDeferredAudioThreadActions();
            serviceRecoveryAcknowledgement();
            serviceLibraryRecovery();
            servicePresetSelectionAcknowledgement();
            serviceCandidatePreparationAcknowledgement();
            const auto currentTransportHeartbeat = transportHeartbeat.load (
                std::memory_order_acquire);
            if (! blind.auditioning())
            {
                observedTransportHeartbeat = currentTransportHeartbeat;
                missedTransportCallbacks = 0;
            }
            else if (! latestPlaying.load (std::memory_order_acquire)
                     || ! latestPositionValid.load (std::memory_order_acquire))
            {
                invalidateBlind();
                missedTransportCallbacks = 0;
            }
            else if (currentTransportHeartbeat != observedTransportHeartbeat)
            {
                observedTransportHeartbeat = currentTransportHeartbeat;
                missedTransportCallbacks = 0;
            }
            else if (++missedTransportCallbacks >= workspacePolls)
            {
                invalidateBlind();
                missedTransportCallbacks = 0;
            }
            if (selectionGeneration != appliedSelectionGeneration || untilPoll-- <= 0)
            {
                refreshWorkspace (configuration, juce::Time::currentTimeMillis());
                untilPoll = workspacePolls;
            }
            wait (workerPollMs);
        }
    }
}
