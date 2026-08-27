#include "PreDisplayController.h"

#include "PreDisplayPresence.h"
#include "PreDisplayProjection.h"
#include "PreDisplayProtocol.h"
#include "PreDisplayRepository.h"

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <unistd.h>
#endif

namespace hypha::pre_display
{
    namespace
    {
        constexpr int guideScanEveryPolls = 5;

        std::uint32_t currentProcessId() noexcept
        {
           #if JUCE_WINDOWS
            return static_cast<std::uint32_t> (::GetCurrentProcessId());
           #else
            return static_cast<std::uint32_t> (::getpid());
           #endif
        }
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
        if (root.getFullPathName().isEmpty()
            || ! safeId (identityIn.instanceId) || ! safeId (identityIn.projectUuid)
            || (identityIn.dawSessionUuid.isNotEmpty() && ! safeId (identityIn.dawSessionUuid)))
            return;
        if (identityIn.hostProcessId == 0)
            identityIn.hostProcessId = currentProcessId();

        juce::File previousPresenceFile;
        juce::File previousAcknowledgementFile;
        juce::File previousCapabilityFile;
        juce::File currentPresenceFile;
        juce::File currentAcknowledgementFile;
        juce::File currentCapabilityFile;
        {
            const juce::ScopedLock lock (identityLock);
            previousPresenceFile = ownPresenceFile;
            previousAcknowledgementFile = ownAcknowledgementFile;
            previousCapabilityFile = ownCapabilityFile;
            identity = std::move (identityIn);
            configured = true;
            ownPresenceFile = root.getChildFile ("presence")
                                  .getChildFile (identity.instanceId + ".json");
            ownAcknowledgementFile = root.getChildFile ("ack")
                                         .getChildFile (identity.instanceId + ".json");
            ownCapabilityFile = root.getChildFile ("capability")
                                    .getChildFile (identity.instanceId + ".json");
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
                    writeCapability (root, currentIdentity, now);
                    writePresence (root, currentIdentity,
                                   workerState->clock.snapshot(),
                                   workerState->clock.observedAtMs(), now);
                    receipt = workerState->repository.refresh (workerState->guide);
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
                            .getChildFile (currentIdentity.instanceId + ".json"));
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
