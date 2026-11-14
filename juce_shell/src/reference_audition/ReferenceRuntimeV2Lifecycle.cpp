#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceTestCommitObserver.h"

#include <algorithm>
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
        clearPreparedWorkspace();
        retirePositionSearch();
        retireSourceVerification();
        clearSongPreload();
        songBankRetiringSinceMs = 0;
        pages.close(); preparedPages.close();
        activePages.store (&pages, std::memory_order_release);
        aCapture.disconnect();
        { const juce::ScopedLock lock (stateLock); workspace.reset(); }  // visualBinding がメッセージスレッドから読む
        activeABinding.reset();
        activeSourceKey.clear();
        activeMappingKey.clear();
        activeContentMappingKey.clear();
        contentMappingContinuity.store (0, std::memory_order_release);
        copyCorrespondence.store (false, std::memory_order_release);
        activePublishedSelectionKey.clear();
        workerSource.reset(); workerSourceGeneration = 0;
        sourceCache.clear();
        {
            const juce::ScopedLock mappingLock (mappingWriteLock);
            mappingGeneration.fetch_add (1, std::memory_order_acq_rel);
            cueStart.store (0, std::memory_order_relaxed);
            cueEnd.store (0, std::memory_order_relaxed);
            cueLoops.store (false, std::memory_order_relaxed);
            sampleLocked.store (false, std::memory_order_relaxed);
            bHostAnchor.store (0, std::memory_order_relaxed);
            bSourceAnchor.store (0, std::memory_order_relaxed);
            followOffset.store (0, std::memory_order_relaxed);  // 流し方（followA）は利用者の選択なので残す
            mappingGeneration.fetch_add (1, std::memory_order_release);
        }
        blindContextKey.clear();
        blindPreparationKey.clear();
        calibrationObservation.clear();
        activePresetAdoptionKey.clear();
        blind.clear();
        {
            const juce::ScopedLock lock (stateLock);
            publishedSource.reset();
            libraryPreparation.reset();
            adoptedVersions.reset();
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
        appliedSelectionGeneration.store (0, std::memory_order_release);
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

    void RuntimeV2Controller::serviceACapture (const Configuration& configuration)
    {
        aCapture.service (
            activeABinding,
            static_cast<std::int64_t> (std::llround (configuration.sampleRate)),
            configuration.channels,
            juce::Time::currentTimeMillis(),
            versionComparison && configuration.identity.library
                ? configuration.identity.runtimeInstanceId : juce::String {});
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
            // Race fixtures request one ordinary worker read; they never fabricate a publication.
            if (KIRIN_REFERENCE_REFRESH_FOR_TEST (this)) untilPoll = 0;
            currentPages().service();
            if (contentRefreshRequested.exchange (false, std::memory_order_acq_rel)
                && !bSelected.load (std::memory_order_acquire) && !blind.ongoing())
            {
                blind.clear(); blindPreparationKey.clear(); calibrationObservation.clear();
                retirePositionSearch();
                aCapture.disconnect(); untilPoll = 0;
            }
            KIRIN_REFERENCE_BEFORE_CAPTURE_FOR_TEST (this);
            serviceACapture (configuration);
            serviceRuntimeEvents();
            serviceDeferredAudioThreadActions();
            serviceRecoveryAcknowledgement();
            serviceLibraryRecovery();
            if (reportsLibraryReceived.load (std::memory_order_acquire))
                libraryReceivedWriter.service (juce::Time::currentTimeMillis());
            servicePresetSelectionAcknowledgement();
            serviceCandidatePreparationAcknowledgement();
            const auto currentTransportHeartbeat = transportHeartbeat.load (
                std::memory_order_acquire);
            if (! blind.auditioning())
            {
                observedTransportHeartbeat = currentTransportHeartbeat;
                missedTransportCallbacks = 0;
            }
            else if (versionComparison && !latestPlaying.load (std::memory_order_acquire))
            {
                missedTransportCallbacks = 0;
                observedTransportHeartbeat = currentTransportHeartbeat;
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
            // 選択を替えた役がまだ鳴っている（フェード中）あいだは、古い音の準備を崩さない。
            const bool retiring = serviceSwitchRetirement();
            const bool prepared = !retiring && servicePreparedWorkspace (configuration);
            const bool fading = retiring || deferredRevokeWaiting();
            if (positionSearchJustFinished()) untilPoll = 0;
            // 音源の確かめを待つ間も Library の更新は定期に見る（鳴っている役が消えたら hash を待たずに止める）。
            const bool selectionWaits = selectionGeneration != appliedSelectionGeneration && !verifyingSource (selectionGeneration);
            if (sourceCheckSlotFreed()) untilPoll = 0;
            const bool sourceChecked = sourceVerification.check.valid()
                && sourceVerification.check.wait_for (std::chrono::seconds (0)) != std::future_status::timeout;
            if (! fading && !prepared && (selectionWaits || sourceChecked || untilPoll-- <= 0))
            {
                refreshWorkspace (configuration, juce::Time::currentTimeMillis());
                if (versionComparison && workspace != nullptr)
                    versionIdentifier.prepare (root, *workspace, juce::Time::currentTimeMillis());
                untilPoll = workspacePolls;
                if (std::exchange (sourceRecheckSoon, false))
                {
                    untilPoll = sourceRecheckPolls;
                    sourceRecheckPolls = std::min (workspacePolls, sourceRecheckPolls * 2);
                }
            }
            if (songsOnly.load (std::memory_order_acquire)) serviceSongPreload();
            wait (fading ? 2 : workerPollMs);
        }
    }
}
