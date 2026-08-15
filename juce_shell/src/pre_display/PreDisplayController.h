#pragma once

#include <juce_core/juce_core.h>

#include "PreDisplayClock.h"

namespace hypha::pre_display
{
    enum class DisplayStatus
    {
        none,
        received,
        waitingForProjectClock,
        active,
        next,
        end,
    };

    struct DisplaySnapshot
    {
        DisplayStatus status = DisplayStatus::none;
        juce::String guideId;
        juce::String contentHash;
        juce::String payloadKind;
        juce::String primary;
        juce::String detail;
    };

    struct RuntimeIdentity
    {
        juce::String instanceId;
        juce::String projectUuid;
        juce::String dawSessionUuid;
        juce::String name;
        juce::String pluginVersion;
        juce::String pluginFormat;
        juce::String platform;
        juce::String architecture;
        std::uint32_t hostProcessId = 0;
    };

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
        bool writePresence (const RuntimeIdentity&, const ClockSnapshot&);
        void refreshActiveGuide();
        DisplaySnapshot projectDisplay (const ClockSnapshot&, std::int64_t nowMs) const;
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
