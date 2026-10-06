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
        GuidePresentationSnapshot guidePresentationSnapshot() const
        {
            const juce::ScopedLock lock (displayLock);
            return guidePresentation;
        }
        ConnectionRequest pendingConnection() const;
        bool acceptPendingConnection();
        WorkReference connectedWorkReference() const;
        juce::String connectedWorkTitle() const;

        static juce::File transportRoot();
        // 試験だけが使う：これから作る Controller の書き先を home の代わりに root の下にする（空の File で本物の
        // 場所に戻す）。macOS の JUCE のホームは HOME に従わないので、試験の保存先の付け替えだけでは届かない。製品は呼ばない。
        static void placeUnderForTest (const juce::File& root);

    private:
        struct WorkerState;

        void run() override;
        void publishDisplay (DisplaySnapshot, GuidePresentationSnapshot);
        void removeOwnLeaseFiles();

        const ClockTap& clockTap;
        const juce::File root;
        mutable juce::CriticalSection identityLock;
        RuntimeIdentity identity;
        WorkReference acceptedWorkReference;
        bool configured = false;
        mutable juce::CriticalSection displayLock;
        DisplaySnapshot display;
        GuidePresentationSnapshot guidePresentation;
        mutable juce::CriticalSection connectionLock;
        ConnectionRequest connectionRequest;
        juce::File ownPresenceFile;
        juce::File ownAcknowledgementFile;
        juce::File ownCapabilityFile;
        std::unique_ptr<WorkerState> workerState;
    };
}
