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
        selectA();
        removeRuntimeFiles (activeRuntimeFiles);
        activeRuntimeFiles = {};
        ready.store (false, std::memory_order_release);
        pages.close();
        aCapture.disconnect();
        workspace.reset();
        activeABinding.reset();
        activeSource.reset();
        activeSourceArtifactSha256.clear();
        activeSourceKey.clear();
        activeMappingKey.clear();
        activeContentMappingKey.clear();
        mappingGeneration.fetch_add (1, std::memory_order_acq_rel);
        cueStart.store (0, std::memory_order_relaxed);
        cueEnd.store (0, std::memory_order_relaxed);
        cueLoops.store (false, std::memory_order_relaxed);
        sampleLocked.store (false, std::memory_order_relaxed);
        bHostAnchor.store (0, std::memory_order_relaxed);
        bSourceAnchor.store (0, std::memory_order_relaxed);
        mappingGeneration.fetch_add (1, std::memory_order_release);
        pendingApprovalKey.clear();
        blindContextKey.clear();
        blindPreparationKey.clear();
        blind.clear();
        {
            const juce::ScopedLock lock (stateLock);
            auditionEventSessions.clear();
            blindEventSession.reset();
            pendingRecoveryRequest.reset();
            activeEventContext = {};
            activeEventCandidate = {};
            activeEventCue = {};
            activeEventSource.reset();
            currentSnapshot.recoveryStatus.clear();
            recoveryStatusExpiresAtMs = 0;
        }
        appliedConfigurationGeneration = configuration.generation;
        appliedSelectionGeneration = 0;
        if (! configuration.identity.valid() || ! std::isfinite (configuration.sampleRate)
            || configuration.sampleRate <= 0.0
            || (configuration.channels != 1 && configuration.channels != 2))
        {
            publish ({});
            return;
        }
        activeRuntimeFiles = runtimeFiles (root, configuration.identity);
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
            serviceDeferredAudioThreadActions();
            serviceRuntimeEvents();
            serviceRecoveryAcknowledgement();
            if (selectionGeneration != appliedSelectionGeneration || untilPoll-- <= 0)
            {
                refreshWorkspace (configuration, juce::Time::currentTimeMillis());
                untilPoll = workspacePolls;
            }
            wait (workerPollMs);
        }
    }
}
