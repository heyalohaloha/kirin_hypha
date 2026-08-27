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
        explicit Controller (const ClockTap& clockTapIn,
                             juce::File transportRootIn = transportRoot());
        ~Controller() override;

        void configureAndStart (RuntimeIdentity identityIn);
        void setName (const juce::String& name);
        DisplaySnapshot displaySnapshot() const;

        static juce::File transportRoot();

    private:
        struct WorkerState;

        void run() override;
        void publishDisplay (DisplaySnapshot);
        void removeOwnLeaseFiles();

        const ClockTap& clockTap;
        const juce::File root;
        mutable juce::CriticalSection identityLock;
        RuntimeIdentity identity;
        bool configured = false;
        mutable juce::CriticalSection displayLock;
        DisplaySnapshot display;
        juce::File ownPresenceFile;
        juce::File ownAcknowledgementFile;
        juce::File ownCapabilityFile;
        std::unique_ptr<WorkerState> workerState;
    };
}
