#include "PreDisplayController.h"

#include "PreDisplayPresence.h"
#include "PreDisplayProjection.h"
#include "PreDisplayProtocol.h"
#include "PreDisplayRepository.h"

namespace hypha::pre_display
{
    namespace
    {
        constexpr int guideScanEveryPolls = 5;
    }

    struct Controller::WorkerState
    {
        explicit WorkerState (juce::File transportRoot)
            : repository (std::move (transportRoot)) {}

        GuideModel guide;
        ClockReaderState clock;
        GuideRepository repository;
        int pollsUntilGuideScan = 0;
    };

    Controller::Controller (const ClockTap& clockTapIn, juce::File transportRootIn)
        : juce::Thread ("Kirin PRE display"),
          clockTap (clockTapIn),
          root (std::move (transportRootIn)),
          workerState (std::make_unique<WorkerState> (root))
    {
    }

    Controller::~Controller()
    {
        signalThreadShouldExit();
        notify();
        if (! stopThread (-1))
            jassertfalse;
        removeOwnLeaseFiles();
    }

    void Controller::configureAndStart (RuntimeIdentity identityIn)
    {
        if (root.getFullPathName().isEmpty())
            return;

        juce::File previousPresenceFile;
        juce::File previousAcknowledgementFile;
        juce::File previousCapabilityFile;
        juce::File currentPresenceFile;
        juce::File currentAcknowledgementFile;
        juce::File currentCapabilityFile;
        {
            const juce::ScopedLock lock (identityLock);
            const auto lease = prepareRuntimeLease (root, std::move (identityIn), identity, configured);
            if (! lease.valid())
                return;
            previousPresenceFile = ownPresenceFile;
            previousAcknowledgementFile = ownAcknowledgementFile;
            previousCapabilityFile = ownCapabilityFile;
            identity = lease.identity;
            configured = true;
            ownPresenceFile = lease.presenceFile;
            ownAcknowledgementFile = lease.acknowledgementFile;
            ownCapabilityFile = lease.capabilityFile;
            currentPresenceFile = ownPresenceFile;
            currentAcknowledgementFile = ownAcknowledgementFile;
            currentCapabilityFile = ownCapabilityFile;
        }
        if (previousPresenceFile != currentPresenceFile)
            removePresence (previousPresenceFile);
        if (previousAcknowledgementFile != currentAcknowledgementFile)
            removeAcknowledgement (previousAcknowledgementFile);
        if (previousCapabilityFile != currentCapabilityFile)
            removeCapability (previousCapabilityFile);
        if (! isThreadRunning())
            startThread (juce::Thread::Priority::low);
        notify();
    }

    void Controller::setName (const juce::String& name)
    {
        const juce::ScopedLock lock (identityLock);
        identity.name = name.substring (0, 64);
    }

    DisplaySnapshot Controller::displaySnapshot() const
    {
        const juce::ScopedLock lock (displayLock);
        return display;
    }

    ConnectionRequest Controller::pendingConnection() const
    {
        const juce::ScopedLock lock (connectionLock);
        return connectionRequest;
    }

    bool Controller::acceptPendingConnection()
    {
        ConnectionRequest request;
        {
            const juce::ScopedLock lock (connectionLock);
            request = connectionRequest;
        }
        if (! request.validAt (juce::Time::currentTimeMillis()))
            return false;
        {
            const juce::ScopedLock lock (identityLock);
            if (! configured)
                return false;
            identity.workId = request.workId;
            identity.bindingId = request.bindingId;
        }
        notify();
        return true;
    }

    void Controller::run()
    {
        while (! threadShouldExit())
        {
            RuntimeIdentity currentIdentity;
            {
                const juce::ScopedLock lock (identityLock);
                if (configured)
                    currentIdentity = identity;
            }

            if (safeId (currentIdentity.instanceId))
            {
                const auto now = juce::Time::currentTimeMillis();
                workerState->clock.refresh (clockTap, now);
                GuideReceipt receipt;
                bool scannedGuide = false;
                if (workerState->pollsUntilGuideScan <= 0)
                {
                    const auto pending = workerState->repository.pendingConnection (now, currentIdentity);
                    {
                        const juce::ScopedLock lock (connectionLock);
                        connectionRequest = pending;
                    }
                    writeCapability (root, currentIdentity, now);
                    writePresence (root, currentIdentity,
                                   workerState->clock.snapshot(),
                                   workerState->clock.observedAtMs(), now);
                    receipt = workerState->repository.refresh (workerState->guide, currentIdentity);
                    scannedGuide = true;
                    workerState->pollsUntilGuideScan = guideScanEveryPolls;
                }
                --workerState->pollsUntilGuideScan;
                const auto nextDisplay = projectDisplay (workerState->guide,
                                                         workerState->clock.snapshot(),
                                                         workerState->clock.observedAtMs(), now);
                publishDisplay (nextDisplay);
                if (scannedGuide)
                {
                    if (receipt.state == GuideRefreshState::accepted
                        && workerState->guide.valid())
                        writeAcknowledgement (root, currentIdentity,
                                              workerState->guide, nextDisplay, now);
                    else if (receipt.state == GuideRefreshState::rejected)
                        writeRejectedAcknowledgement (root, currentIdentity, receipt, now);
                    else if (receipt.state == GuideRefreshState::cleared)
                        removeAcknowledgement (root.getChildFile ("ack")
                            .getChildFile (currentIdentity.runtimeInstanceId + ".json"));
                }
            }
            wait (static_cast<int> (projectionPollMs));
        }
    }

    void Controller::publishDisplay (DisplaySnapshot next)
    {
        const juce::ScopedLock lock (displayLock);
        display = std::move (next);
    }

    void Controller::removeOwnLeaseFiles()
    {
        juce::File presenceFile;
        juce::File acknowledgementFile;
        juce::File capabilityFile;
        {
            const juce::ScopedLock lock (identityLock);
            presenceFile = ownPresenceFile;
            acknowledgementFile = ownAcknowledgementFile;
            capabilityFile = ownCapabilityFile;
        }
        removePresence (presenceFile);
        removeAcknowledgement (acknowledgementFile);
        removeCapability (capabilityFile);
    }
}
