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

    Controller::Controller (const ClockTap& clockTapIn)
        : juce::Thread ("Kirin PRE display"),
          clockTap (clockTapIn),
          workerState (std::make_unique<WorkerState> (transportRoot()))
    {
    }

    Controller::~Controller()
    {
        signalThreadShouldExit();
        notify();
        stopThread (2'000);
        removeOwnPresence();
    }

    void Controller::configureAndStart (RuntimeIdentity identityIn)
    {
        if (! safeId (identityIn.instanceId) || ! safeId (identityIn.projectUuid))
            return;
        if (identityIn.hostProcessId == 0)
            identityIn.hostProcessId = currentProcessId();

        juce::File previousPresenceFile;
        juce::File currentPresenceFile;
        {
            const juce::ScopedLock lock (identityLock);
            previousPresenceFile = ownPresenceFile;
            identity = std::move (identityIn);
            configured = true;
            ownPresenceFile = transportRoot().getChildFile ("presence")
                                             .getChildFile (identity.instanceId + ".json");
            currentPresenceFile = ownPresenceFile;
        }
        if (previousPresenceFile != currentPresenceFile)
            removePresence (previousPresenceFile);
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
            local = juce::File::getSpecialLocation (
                juce::File::userApplicationDataDirectory).getFullPathName();
        return juce::File (local).getChildFile ("Kirin OS").getChildFile ("plugin_data")
                                 .getChildFile ("pre_display").getChildFile ("v1");
       #else
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
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
                    writePresence (transportRoot(), currentIdentity,
                                   workerState->clock.snapshot(),
                                   workerState->clock.observedAtMs(), now);
                    workerState->repository.refresh (workerState->guide);
                    workerState->pollsUntilGuideScan = guideScanEveryPolls;
                }
                --workerState->pollsUntilGuideScan;
                publishDisplay (projectDisplay (workerState->guide,
                                                workerState->clock.snapshot(),
                                                workerState->clock.observedAtMs(), now));
            }
            wait (static_cast<int> (projectionPollMs));
        }
    }

    void Controller::publishDisplay (DisplaySnapshot next)
    {
        const juce::ScopedLock lock (displayLock);
        display = std::move (next);
    }

    void Controller::removeOwnPresence()
    {
        juce::File file;
        {
            const juce::ScopedLock lock (identityLock);
            file = ownPresenceFile;
        }
        removePresence (file);
    }
}
