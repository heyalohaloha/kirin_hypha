#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <optional>
#include "UpdateStore.h"
namespace hypha::update
{
inline constexpr std::int64_t checkIntervalSeconds = 24 * 60 * 60;
enum class Status { unchecked, current, available, development, failed, unavailable, withdrawn };
struct Snapshot
{
    bool enabled = false, busy = false;
    Status status = Status::unchecked;
    juce::String version;
    std::int64_t checkedAt = 0, nextAttemptAt = 0;
    std::int64_t publishedAt = 0, expiresAt = 0;
    std::uint64_t revision = 0;
    std::uint64_t manualCompletion = 0, preferenceCompletion = 0;
    bool preferenceSaved = false, preferenceValue = false;
};

// HTTP is an injected boundary in tests. A null response is a failure/cancellation;
// it consumes the same persisted daily budget as a successful response.
using Fetch = std::function<std::optional<juce::String> (const std::atomic<bool>&)>;
struct Configuration
{
    juce::File root;
    juce::String version, sourceCommit, publicKey, platform, format = "VST3";
    Fetch fetch;
    std::function<std::int64_t()> now;
};

// Non-RT service owned by live processors, never by module static storage. All storage,
// cryptography and HTTP run on one worker. Snapshot/notification access is memory-only.
class Checker
{
public:
    explicit Checker (Configuration);
    ~Checker();
    static std::shared_ptr<Checker> shared (const juce::String& format = "VST3");
    Snapshot snapshot() const;
    std::uint64_t request (bool manual);
    std::uint64_t setEnabled (bool);
    bool claimNotification();

private:
    void run();
    void perform (bool manual, std::optional<bool> preference,
                  std::uint64_t manualToken, std::uint64_t preferenceToken);
    void publish (const Snapshot&);
    std::int64_t readClock() const noexcept;
    Configuration configuration;
    mutable std::mutex mutex;
    std::condition_variable wake;
    Snapshot view;
    bool stop = false, pending = false, manualPending = false;
    bool refreshRequired = false; // Worker-only: contention cannot permanently guess a saved OFF.
    std::optional<bool> preferencePending;
    std::uint64_t nextAction = 0, pendingManualToken = 0, pendingPreferenceToken = 0;
    std::atomic<bool> cancelled { false }, notification { false };
    std::thread worker;
};
}
