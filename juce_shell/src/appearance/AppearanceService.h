#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

#include "AppearanceStorage.h"

namespace hypha::appearance
{
enum class UserActionState { none, pending, persisted, failed, unsupported };

struct UserActionReceipt final
{
    std::uint64_t id = 0;
    UserActionState state = UserActionState::none;
};

// One service is shared by every editor in a loaded PRE or POST binary. Existing editor timers only
// call pulse(); all filesystem reads, JSON parsing, inter-module locking, and writes stay on this
// background worker. No service instance is created by audio processing or offline rendering.
class Service final
{
public:
    explicit Service (juce::File sharedPluginDataRoot = Storage::defaultSharedPluginDataRoot());
    ~Service();

    static Service& shared();

    void editorBecameVisible();
    void editorBecameHidden();
    void pulse();
    Snapshot snapshot() const;

    UserActionReceipt setChoice (Choice);
    UserActionReceipt acknowledgeNotice();
    UserActionReceipt latestUserAction() const;

    void refreshSynchronouslyForTest();

private:
    void ensureWorkerStarted();
    void run();
    void performWork (std::optional<Choice>, bool, std::uint64_t);
    void publishSnapshot (const Preference&, bool);

    mutable std::mutex stateMutex;
    std::condition_variable wake;
    Storage storage;
    std::thread worker;
    Snapshot current;
    UserActionReceipt lastUserAction;
    std::optional<Choice> pendingChoice;
    std::size_t visibleEditors = 0;
    std::uint64_t nextActionId = 1;
    std::uint64_t pendingActionId = 0;
    std::optional<Choice> volatileChoice;
    std::int64_t volatileAtRevision = 0;
    bool pendingAcknowledgement = false;
    bool refreshRequested = false;
    bool jobActive = false;
    bool exiting = false;
    std::chrono::steady_clock::time_point lastRefreshScheduled {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Service)
};
}
