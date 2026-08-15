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
        constexpr std::int64_t projectionPollMs = 100;
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
        const auto stoppedCleanly = stopThread (-1);
        jassert (stoppedCleanly);
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
        juce::File currentPresenceFile;
        juce::File currentAcknowledgementFile;
        {
            const juce::ScopedLock lock (identityLock);
            previousPresenceFile = ownPresenceFile;
            previousAcknowledgementFile = ownAcknowledgementFile;
            identity = std::move (identityIn);
            configured = true;
            ownPresenceFile = root.getChildFile ("presence")
                                  .getChildFile (identity.instanceId + ".json");
            ownAcknowledgementFile = root.getChildFile ("ack")
                                         .getChildFile (identity.instanceId + ".json");
            currentPresenceFile = ownPresenceFile;
            currentAcknowledgementFile = ownAcknowledgementFile;
        }
        if (previousPresenceFile != currentPresenceFile)
            removePresence (previousPresenceFile);
        if (previousAcknowledgementFile != currentAcknowledgementFile)
            removeAcknowledgement (previousAcknowledgementFile);
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

    juce::File Controller::transportRoot()
    {
       #if JUCE_WINDOWS
        auto local = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});
        if (local.isEmpty())
            local = juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
                        .getFullPathName();
        if (local.isEmpty())
        {
            const auto profile = juce::SystemStats::getEnvironmentVariable ("USERPROFILE", {});
            if (profile.isNotEmpty())
                local = juce::File (profile).getChildFile ("AppData").getChildFile ("Local")
                                            .getFullPathName();
        }
        if (local.isEmpty())
            return {};
        return juce::File (local).getChildFile ("Kirin OS").getChildFile ("plugin_data")
                                 .getChildFile ("pre_display").getChildFile ("v1");
       #else
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        if (home.getFullPathName().isEmpty())
            return {};
        return home
            .getChildFile ("Library").getChildFile ("Application Support")
            .getChildFile ("Kirin OS").getChildFile ("plugin_data")
            .getChildFile ("pre_display").getChildFile ("v1");
       #endif
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
                if (workerState->pollsUntilGuideScan <= 0)
                {
                    writePresence (root, currentIdentity,
                                   workerState->clock.snapshot(),
                                   workerState->clock.observedAtMs(), now);
                    workerState->repository.refresh (workerState->guide);
                    workerState->pollsUntilGuideScan = guideScanEveryPolls;
                }
                --workerState->pollsUntilGuideScan;
                const auto nextDisplay = projectDisplay (workerState->guide,
                                                         workerState->clock.snapshot(),
                                                         workerState->clock.observedAtMs(), now);
                publishDisplay (nextDisplay);
                if (workerState->pollsUntilGuideScan == guideScanEveryPolls - 1)
                {
                    if (workerState->guide.valid())
                        writeAcknowledgement (root, currentIdentity,
                                              workerState->guide, nextDisplay, now);
                    else
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
        {
            const juce::ScopedLock lock (identityLock);
            presenceFile = ownPresenceFile;
            acknowledgementFile = ownAcknowledgementFile;
        }
        removePresence (presenceFile);
        removeAcknowledgement (acknowledgementFile);
    }
}
