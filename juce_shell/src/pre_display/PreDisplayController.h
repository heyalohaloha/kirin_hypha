#pragma once

#include <memory>

#include <juce_core/juce_core.h>

#include "PreDisplayClock.h"
#include "PreDisplayModel.h"

namespace hypha::pre_display
{
    class Controller final : private juce::Thread
    {
    public:
        explicit Controller (const ClockTap& clockTapIn);
        ~Controller() override;

        void configureAndStart (RuntimeIdentity identityIn);
        void setName (const juce::String& name);
        DisplaySnapshot displaySnapshot() const;

        static juce::File transportRoot();

    private:
        struct WorkerState;

        void run() override;
        void publishDisplay (DisplaySnapshot);
        void removeOwnPresence();

        const ClockTap& clockTap;
        mutable juce::CriticalSection identityLock;
        RuntimeIdentity identity;
        bool configured = false;
        mutable juce::CriticalSection displayLock;
        DisplaySnapshot display;
        juce::File ownPresenceFile;
        std::unique_ptr<WorkerState> workerState;
    };
}
