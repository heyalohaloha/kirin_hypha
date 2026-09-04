#include "ReferenceAuditionController.h"

#include <cmath>
#include <utility>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <unistd.h>
#endif

namespace hypha::reference_audition
{
    namespace
    {
        constexpr int workerPollMs = 10;
        constexpr int preparationPolls = 50;
        constexpr double auditionStaleMs = 3'000.0;

        float gainFromDecibels (double gainDb)
        {
            return static_cast<float> (std::pow (10.0, gainDb / 20.0));
        }

        std::uint32_t currentProcessId() noexcept
        {
           #if JUCE_WINDOWS
            return static_cast<std::uint32_t> (::GetCurrentProcessId());
           #else
            return static_cast<std::uint32_t> (::getpid());
           #endif
        }

    }

    Controller::Controller (juce::File transportRootIn, SelectionGate selectionGateIn,
                            RandomBit randomBitIn)
        : juce::Thread ("Kirin Reference audition"),
          root (std::move (transportRootIn)),
          selectionGate (std::move (selectionGateIn)),
          randomBit (randomBitIn ? std::move (randomBitIn) : RandomBit { secureRandomBit }),
          repository (root)
    {
        startThread (juce::Thread::Priority::low);
    }

    Controller::~Controller()
    {
        endBlind();
        signalThreadShouldExit();
        notify();
        if (! stopThread (-1))
            jassertfalse;
        removeRuntimeFiles (activeRuntimeFiles);
    }

    void Controller::configure (RuntimeIdentity identity, double hostSampleRate, int hostChannels)
    {
        if (identity.hostProcessId == 0)
            identity.hostProcessId = currentProcessId();
        {
            const juce::ScopedLock lock (configurationLock);
            requestedConfiguration.identity = std::move (identity);
            requestedConfiguration.sampleRate = hostSampleRate;
            requestedConfiguration.channels = hostChannels;
            ++requestedConfiguration.generation;
        }
        invalidateBlind();
        notify();
    }

    void Controller::disconnect()
    {
        configure ({}, 0.0, 0);
    }

    Snapshot Controller::snapshot() const
    {
        const juce::ScopedLock lock (snapshotLock);
        auto result = currentSnapshot;
        result.bSelected = bSelected.load (std::memory_order_acquire);
        result.transportPlaying = latestPlaying.load (std::memory_order_acquire);
        result.transportPositionValid = latestPositionValid.load (std::memory_order_acquire);
        result.auditionBuffered = result.transportPositionValid
            && ready.load (std::memory_order_acquire)
            && pages.readyAt (mappedSourcePosition (latestHostPosition.load()), 1);
        const auto blind = blindSession.publicState();
        result.blindPhase = blind.phase;
        result.activeBlindStimulus = blind.activeStimulus;
        result.pendingBlindStimulus = blind.pendingStimulus;
        if (blind.phase == BlindPhase::revealed)
            result.blindReveal = blind.revealedStimulusOneSide == static_cast<int> (BlindSide::b)
                ? "1 = B  /  2 = A" : "1 = A  /  2 = B";
        else
            result.blindReveal.clear();
        return result;
    }

    void Controller::publish (Snapshot next)
    {
        const juce::ScopedLock lock (snapshotLock);
        next.bSelected = bSelected.load (std::memory_order_acquire);
        if (next.bSelected || blindSession.ongoing())
        {
            next.appliedGainDb = currentSnapshot.appliedGainDb;
            next.gainLimited = currentSnapshot.gainLimited;
            next.aIntegratedLoudness = currentSnapshot.aIntegratedLoudness;
            next.aMaximumTruePeakDbtp = currentSnapshot.aMaximumTruePeakDbtp;
            next.adjustedBIntegratedLoudness = currentSnapshot.adjustedBIntegratedLoudness;
            next.adjustedBMaximumTruePeakDbtp = currentSnapshot.adjustedBMaximumTruePeakDbtp;
            next.loudnessDeltaBMinusA = currentSnapshot.loudnessDeltaBMinusA;
            next.truePeakDeltaBMinusA = currentSnapshot.truePeakDeltaBMinusA;
        }
        currentSnapshot = std::move (next);
    }

    void Controller::observeTransport (std::int64_t hostPosition, bool positionValid,
                                       bool playing) noexcept
    {
        latestPlaying.store (playing, std::memory_order_release);
        latestPositionValid.store (positionValid, std::memory_order_release);
        if (! positionValid)
            return;
        latestHostPosition.store (hostPosition, std::memory_order_release);
        const auto sourcePosition = mappedSourcePosition (hostPosition);
        if (sourcePosition >= 0)
            pages.request (sourcePosition);
    }

    bool Controller::prepareReferenceGain (double aIntegratedLoudness,
                                           double aMaximumTruePeakDbtp) noexcept
    {
        if (! ready.load (std::memory_order_acquire)
            || ! latestPositionValid.load (std::memory_order_acquire)
            || ! std::isfinite (aIntegratedLoudness)
            || ! std::isfinite (aMaximumTruePeakDbtp))
            return false;
        Preparation preparation;
        SourceReceipt receipt;
        {
            const juce::ScopedLock lock (snapshotLock);
            preparation = activePreparation;
            receipt = activeReceipt;
        }
        if (! preparation.valid() || ! receipt.valid())
            return false;
        const double requiredGain = aIntegratedLoudness - receipt.integratedLoudness;
        const double appliedGain = requiredGain > 0.0
            ? juce::jmin (requiredGain, preparation.maxSafePositiveGainDb)
            : juce::jmax (-100.0, requiredGain);
        if (receipt.maximumTruePeakDbtp + appliedGain > -1.0 + 1.0e-9)
            return false;
        const auto hostPosition = latestHostPosition.load (std::memory_order_acquire);
        bHostAnchor.store (hostPosition, std::memory_order_release);
        bLinearGain.store (gainFromDecibels (appliedGain), std::memory_order_release);
        const auto sourcePosition = mappedSourcePosition (hostPosition);
        pages.request (sourcePosition);
        if (! pages.readyAt (sourcePosition, 1))
            return false;
        {
            const juce::ScopedLock lock (snapshotLock);
            currentSnapshot.appliedGainDb = appliedGain;
            currentSnapshot.gainLimited = requiredGain > appliedGain + 1.0e-9;
            currentSnapshot.aIntegratedLoudness = aIntegratedLoudness;
            currentSnapshot.aMaximumTruePeakDbtp = aMaximumTruePeakDbtp;
            currentSnapshot.adjustedBIntegratedLoudness =
                receipt.integratedLoudness + appliedGain;
            currentSnapshot.adjustedBMaximumTruePeakDbtp =
                receipt.maximumTruePeakDbtp + appliedGain;
            currentSnapshot.loudnessDeltaBMinusA =
                currentSnapshot.adjustedBIntegratedLoudness - aIntegratedLoudness;
            currentSnapshot.truePeakDeltaBMinusA =
                currentSnapshot.adjustedBMaximumTruePeakDbtp - aMaximumTruePeakDbtp;
        }
        return true;
    }

    bool Controller::activatePreparedB() noexcept
    {
        if (! ready.load (std::memory_order_acquire)
            || ! latestPositionValid.load (std::memory_order_acquire))
            return false;
        const auto sourcePosition = mappedSourcePosition (
            latestHostPosition.load (std::memory_order_acquire));
        pages.request (sourcePosition);
        if (! pages.readyAt (sourcePosition, 1))
            return false;
        if (selectionGate && ! selectionGate (true))
            return false;
        bSelected.store (true, std::memory_order_release);
        return true;
    }

    bool Controller::selectB (double aIntegratedLoudness,
                              double aMaximumTruePeakDbtp) noexcept
    {
        if (blindSession.ongoing()
            || ! prepareReferenceGain (aIntegratedLoudness, aMaximumTruePeakDbtp))
            return false;
        return activatePreparedB();
    }

    void Controller::selectAOutput() noexcept
    {
        if (bSelected.exchange (false, std::memory_order_acq_rel) && selectionGate)
            selectionGate (false);
    }

    void Controller::failClosedToA() noexcept
    {
        blindSession.invalidate();
        if (bSelected.exchange (false, std::memory_order_acq_rel))
            gateReleasePending.store (true, std::memory_order_release);
    }

    void Controller::selectA() noexcept
    {
        endBlind();
    }

    bool Controller::startBlind (double aIntegratedLoudness,
                                 double aMaximumTruePeakDbtp) noexcept
    {
        if (blindSession.ongoing()
            || ! latestPlaying.load (std::memory_order_acquire)
            || ! prepareReferenceGain (aIntegratedLoudness, aMaximumTruePeakDbtp))
            return false;
        selectAOutput();
        bool stimulusOneUsesB = false;
        try
        {
            stimulusOneUsesB = randomBit();
        }
        catch (...)
        {
            return false;
        }
        blindSession.begin (stimulusOneUsesB);
        return true;
    }

    bool Controller::selectBlindStimulus (int stimulus) noexcept
    {
        if (! latestPlaying.load (std::memory_order_acquire)
            || ! latestPositionValid.load (std::memory_order_acquire))
            return false;
        BlindSide side = BlindSide::a;
        if (! blindSession.request (stimulus, side))
            return false;
        if (side == BlindSide::b)
        {
            if (activatePreparedB())
                return true;
            blindSession.cancelPendingRequest();
            return false;
        }
        selectAOutput();
        return true;
    }

    bool Controller::revealBlind() noexcept
    {
        return blindSession.reveal();
    }

    void Controller::endBlind() noexcept
    {
        blindSession.end();
        selectAOutput();
    }

    void Controller::invalidateBlind() noexcept
    {
        blindSession.invalidate();
        selectAOutput();
    }

    std::int64_t Controller::mappedSourcePosition (std::int64_t hostPosition) const noexcept
    {
        if (alignmentMode.load (std::memory_order_acquire)
            == static_cast<int> (AlignmentMode::sampleLock))
            return hostPosition;
        return cueSourcePosition.load (std::memory_order_acquire)
             + hostPosition - bHostAnchor.load (std::memory_order_acquire);
    }

    bool Controller::renderSelectedB (juce::AudioBuffer<float>& buffer,
                                      std::int64_t hostPosition,
                                      bool positionValid,
                                      bool auditionAllowed) noexcept
    {
        audioCallbackSequence.fetch_add (1, std::memory_order_relaxed);
        if (! auditionAllowed || ! latestPlaying.load (std::memory_order_acquire)
            || ! positionValid)
        {
            failClosedToA();
            return false;
        }
        if (! bSelected.load (std::memory_order_acquire))
        {
            blindSession.confirmAudible (BlindSide::a);
            return false;
        }
        if (! ready.load (std::memory_order_acquire))
        {
            failClosedToA();
            return false;
        }
        const auto sourcePosition = mappedSourcePosition (hostPosition);
        pages.request (sourcePosition);
        const bool rendered = pages.render (buffer, sourcePosition,
                                            bLinearGain.load (std::memory_order_acquire));
        if (rendered)
            blindSession.confirmAudible (BlindSide::b);
        else
            failClosedToA();
        return rendered;
    }

    void Controller::loseAudibleConfirmation() noexcept
    {
        blindSession.loseAudibleConfirmation();
    }

    void Controller::applyConfiguration (const Configuration& configuration)
    {
        invalidateBlind();
        ready.store (false, std::memory_order_release);
        removeRuntimeFiles (activeRuntimeFiles);
        activeRuntimeFiles = {};
        pages.close();
        {
            const juce::ScopedLock lock (snapshotLock);
            activePreparation = {};
            activeReceipt = {};
        }
        appliedConfigurationGeneration = configuration.generation;
        if (! configuration.identity.valid() || ! std::isfinite (configuration.sampleRate)
            || configuration.sampleRate <= 0.0
            || (configuration.channels != 1 && configuration.channels != 2))
        {
            publish ({});
            return;
        }
        activeRuntimeFiles = runtimeFiles (root, configuration.identity);
        Snapshot next;
        next.state = RuntimeState::waiting;
        publish (std::move (next));
    }

    void Controller::refreshPreparation (const Configuration& configuration, std::int64_t nowMs)
    {
        if (! configuration.identity.valid())
            return;
        writeCapability (root, configuration.identity, nowMs);
        const auto loaded = repository.load (configuration.identity.workId);
        if (loaded.state == LoadState::unavailable)
        {
            if (activeRuntimeFiles.acknowledgement.existsAsFile())
                activeRuntimeFiles.acknowledgement.deleteFile();
            if (activePreparation.valid())
            {
                invalidateBlind();
                ready.store (false, std::memory_order_release);
                pages.close();
                const juce::ScopedLock lock (snapshotLock);
                activePreparation = {};
                activeReceipt = {};
            }
            Snapshot next;
            next.state = RuntimeState::waiting;
            publish (std::move (next));
            return;
        }
        if (! loaded.accepted())
        {
            invalidateBlind();
            ready.store (false, std::memory_order_release);
            if (loaded.preparation.valid())
                writeAcknowledgement (root, configuration.identity, loaded.preparation,
                                      loaded.rejectionCode, nowMs);
            else if (activeRuntimeFiles.acknowledgement.existsAsFile())
                activeRuntimeFiles.acknowledgement.deleteFile();
            Snapshot next;
            next.state = RuntimeState::rejected;
            next.rejectionCode = loaded.rejectionCode;
            publish (std::move (next));
            return;
        }

        const bool changed = loaded.preparation.preparationId != activePreparation.preparationId;
        if (changed)
        {
            invalidateBlind();
            ready.store (false, std::memory_order_release);
            Snapshot verifying;
            verifying.state = RuntimeState::verifying;
            verifying.title = loaded.preparation.title;
            publish (std::move (verifying));
            auto rejection = repository.verifySourceFile (loaded.receipt);
            if (rejection.isEmpty())
                rejection = pages.open (loaded.receipt, configuration.sampleRate,
                                        configuration.channels);
            if (rejection.isNotEmpty())
            {
                {
                    const juce::ScopedLock lock (snapshotLock);
                    activePreparation = loaded.preparation;
                    activeReceipt = loaded.receipt;
                }
                writeAcknowledgement (root, configuration.identity, loaded.preparation,
                                      rejection, nowMs);
                Snapshot next;
                next.state = RuntimeState::rejected;
                next.title = activePreparation.title;
                next.rejectionCode = rejection;
                publish (std::move (next));
                return;
            }
            {
                const juce::ScopedLock lock (snapshotLock);
                activePreparation = loaded.preparation;
                activeReceipt = loaded.receipt;
            }
            alignmentMode.store (static_cast<int> (activePreparation.alignmentMode),
                                 std::memory_order_release);
            cueSourcePosition.store (static_cast<std::int64_t> (
                std::llround (activePreparation.referenceCueSeconds * configuration.sampleRate)),
                std::memory_order_release);
            bHostAnchor.store (latestHostPosition.load (std::memory_order_acquire),
                               std::memory_order_release);
            ready.store (true, std::memory_order_release);
        }

        if (! ready.load (std::memory_order_acquire))
        {
            const auto current = snapshot();
            writeAcknowledgement (root, configuration.identity, loaded.preparation,
                                  current.rejectionCode.isNotEmpty()
                                      ? current.rejectionCode : "source_decode_failed",
                                  nowMs);
            return;
        }

        pages.service();
        writeAcknowledgement (root, configuration.identity, activePreparation, {}, nowMs);
        Snapshot next;
        next.state = RuntimeState::ready;
        next.title = activePreparation.title;
        next.sourceKind = activePreparation.sourceKind;
        next.alignmentMode = activePreparation.alignmentMode;
        next.sourceIntegratedLoudness = activeReceipt.integratedLoudness;
        next.sourceMaximumTruePeakDbtp = activeReceipt.maximumTruePeakDbtp;
        publish (std::move (next));
    }

    void Controller::run()
    {
        int untilPreparationPoll = 0;
        bool monitoringAudio = false;
        auto previousAudioSequence = audioCallbackSequence.load (std::memory_order_acquire);
        double lastAudioActivityMs = juce::Time::getMillisecondCounterHiRes();
        while (! threadShouldExit())
        {
            const auto audioSequence = audioCallbackSequence.load (std::memory_order_acquire);
            const bool shouldMonitor = bSelected.load (std::memory_order_acquire)
                                    || blindSession.ongoing();
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            if (shouldMonitor && monitoringAudio && audioSequence == previousAudioSequence)
            {
                if (nowMs - lastAudioActivityMs >= auditionStaleMs)
                    failClosedToA();
            }
            else if (shouldMonitor)
                lastAudioActivityMs = nowMs;
            monitoringAudio = shouldMonitor;
            previousAudioSequence = audioSequence;
            if (gateReleasePending.exchange (false, std::memory_order_acq_rel) && selectionGate)
                selectionGate (false);
            Configuration configuration;
            {
                const juce::ScopedLock lock (configurationLock);
                configuration = requestedConfiguration;
            }
            if (configuration.generation != appliedConfigurationGeneration)
            {
                applyConfiguration (configuration);
                untilPreparationPoll = 0;
            }
            pages.service();
            if (untilPreparationPoll-- <= 0)
            {
                refreshPreparation (configuration, juce::Time::currentTimeMillis());
                untilPreparationPoll = preparationPolls;
            }
            wait (workerPollMs);
        }
    }
}
