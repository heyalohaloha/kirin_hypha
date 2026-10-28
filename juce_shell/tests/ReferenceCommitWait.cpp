#include "ReferenceControllerTestAccess.h"
#include "../src/reference_audition/ReferenceTestCommitObserver.h"
#include "ReferenceABindingReadClockTests.h"
#include "ReferenceCaptureServiceClockTests.h"
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <cstdlib>
#include <unordered_map>
#include <algorithm>

bool prepareReferenceRuntimeInvocation (int argc, char** argv);

#ifndef KIRIN_REFERENCE_TEST_COMMIT_OBSERVER
 #error Reference commit waiting belongs only to the instrumented native test target
#endif

namespace
{
struct Commits
{
    std::mutex mutex;
    std::condition_variable changed;
    std::uint64_t sequence = 0;
    std::unordered_map<const void*, const char*> workerStages;
    const void* pauseController = nullptr;
    bool pauseEntered = false, pauseReleased = false;
    const void* refreshController = nullptr;
    const void* consumedController = nullptr;
    std::uint64_t refreshIssued = 0, refreshConsumed = 0;
    std::unordered_map<const void*, hypha::reference_audition::ReferenceTestCaptureResetFacts> captureResets;
    std::unordered_map<const void*, hypha::reference_audition::ReferenceTestAuthorityFacts> authorities;
    std::unordered_map<const void*, hypha::reference_audition::ReferenceTestBindingReadFacts> bindingReads;
};
// require() exits without destroying local controllers. Keep test notifications
// alive for the process lifetime, including a failing test's worker shutdown.
Commits& commits() { static auto* state = new Commits(); return *state; }
}

namespace hypha::reference_audition
{
namespace { thread_local ReferenceTestBindingReadScope* bindingProbe = nullptr; }
ReferenceTestBindingReadScope::ReferenceTestBindingReadScope (const void* ownerIn)
    : owner (ownerIn), previous (bindingProbe) { bindingProbe = this; }
ReferenceTestBindingReadScope::~ReferenceTestBindingReadScope()
{
    auto& state = commits();
    const std::lock_guard<std::mutex> lock (state.mutex);
    const auto before = state.bindingReads[owner];
    facts.count = before.count + 1; facts.rejected = before.rejected + (facts.accepted ? 0 : 1);
    facts.lastRejectedStage = facts.accepted ? before.lastRejectedStage : facts.stage;
    facts.lastRejectedAtMs = facts.accepted ? before.lastRejectedAtMs : juce::Time::currentTimeMillis();
    facts.lastRejectedNowMs = facts.accepted ? before.lastRejectedNowMs : facts.nowMs;
    facts.lastRejectedIssuedMs = facts.accepted ? before.lastRejectedIssuedMs : facts.issuedMs;
    facts.lastRejectedExpiresMs = facts.accepted ? before.lastRejectedExpiresMs : facts.expiresMs;
    facts.lastRejectedOsError = facts.accepted ? before.lastRejectedOsError : facts.osError;
    state.bindingReads[owner] = facts; bindingProbe = previous;
}
void errorReferenceTestBindingRead (unsigned long error) noexcept
{ if (bindingProbe) bindingProbe->facts.osError = error; }
void stageReferenceTestBindingRead (const char* stage) noexcept
{ if (bindingProbe) bindingProbe->facts.stage = stage; }
void valuesReferenceTestBindingRead (std::int64_t now, std::int64_t issued, std::int64_t expires) noexcept
{ if (bindingProbe) { bindingProbe->facts.nowMs = now; bindingProbe->facts.issuedMs = issued; bindingProbe->facts.expiresMs = expires; } }
void acceptReferenceTestBindingRead() noexcept
{ if (bindingProbe) { bindingProbe->facts.accepted = true; bindingProbe->facts.stage = "accepted"; } }
ReferenceTestBindingReadFacts referenceTestBindingReadFacts (const void* owner) noexcept
{
    auto& state = commits(); const std::lock_guard<std::mutex> lock (state.mutex);
    const auto found = state.bindingReads.find (owner);
    return found == state.bindingReads.end() ? ReferenceTestBindingReadFacts {} : found->second;
}

bool pauseReferenceTestCaptureWorker (const void* controller, int timeoutMs)
{
    auto& state = commits();
    std::unique_lock<std::mutex> lock (state.mutex);
    if (state.pauseController != nullptr) return false;
    state.pauseController = controller; state.pauseEntered = state.pauseReleased = false;
    if (state.changed.wait_for (lock, std::chrono::milliseconds (timeoutMs), [&] { return state.pauseEntered; })) return true;
    state.pauseController = nullptr; return false;
}
void beforeReferenceTestCaptureService (const void* controller)
{
    auto& state = commits();
    std::unique_lock<std::mutex> lock (state.mutex);
    if (state.pauseController != controller) return;
    state.pauseEntered = true; state.changed.notify_all();
    state.changed.wait (lock, [&] { return state.pauseReleased; });
    state.pauseController = nullptr;
}
void resumeReferenceTestCaptureWorker (const void* controller) noexcept
{
    auto& state = commits();
    { const std::lock_guard<std::mutex> lock (state.mutex);
      if (state.pauseController == controller) state.pauseReleased = true; }
    state.changed.notify_all();
}

void observeReferenceTestAuthority (const void* capture, std::int64_t now, std::int64_t issued,
    std::int64_t expires, bool present, bool valid, bool local) noexcept
{
    auto& state = commits();
    const std::lock_guard<std::mutex> lock (state.mutex);
    auto& facts = state.authorities[capture];
    if (facts.count && now >= facts.nowMs) facts.maxGapMs = std::max (facts.maxGapMs, now - facts.nowMs);
    if (!valid)
    { ++facts.invalidCount; facts.lastInvalidMs = now; facts.lastInvalidIssuedMs = issued; facts.lastInvalidExpiresMs = expires; }
    facts.nowMs = now; facts.issuedMs = issued; facts.expiresMs = expires;
    facts.present = present; facts.valid = valid; facts.local = local; ++facts.count;
}
ReferenceTestAuthorityFacts referenceTestAuthorityFacts (const void* capture) noexcept
{
    auto& state = commits();
    const std::lock_guard<std::mutex> lock (state.mutex);
    const auto found = state.authorities.find (capture);
    return found == state.authorities.end() ? ReferenceTestAuthorityFacts {} : found->second;
}

void notifyReferenceTestCaptureReset (const void* capture, const char* reason, std::int64_t atMs) noexcept
{
    auto& state = commits();
    const std::lock_guard<std::mutex> lock (state.mutex);
    auto& facts = state.captureResets[capture];
    facts.previousReason = facts.reason; facts.previousAtMs = facts.atMs;
    facts.reason = reason; facts.atMs = atMs; ++facts.count;
}

ReferenceTestCaptureResetFacts referenceTestCaptureResetFacts (const void* capture) noexcept
{
    auto& state = commits();
    const std::lock_guard<std::mutex> lock (state.mutex);
    const auto found = state.captureResets.find (capture);
    return found == state.captureResets.end() ? ReferenceTestCaptureResetFacts {} : found->second;
}

namespace { thread_local const void* currentWorkerOwner = nullptr; }
void stageCurrentReferenceTestWorker (const char* stage) noexcept
{ if (currentWorkerOwner) stageReferenceTestWorker (currentWorkerOwner, stage); }
void stageReferenceTestWorker (const void* controller, const char* stage) noexcept
{ currentWorkerOwner = controller; auto& s = commits(); const std::lock_guard<std::mutex> lock (s.mutex); s.workerStages[controller] = stage; }
const char* referenceTestWorkerStage (const void* controller) noexcept
{ auto& s = commits(); const std::lock_guard<std::mutex> lock (s.mutex); const auto f = s.workerStages.find (controller); return f == s.workerStages.end() ? "none" : f->second; }
void notifyReferenceTestCommit() noexcept
{
    auto& state = commits();
    { const std::lock_guard<std::mutex> lock (state.mutex); ++state.sequence; }
    state.changed.notify_all();
}

std::uint64_t requestReferenceTestWorkspaceRefresh (const void* controller) noexcept
{
    auto& state = commits();
    const std::lock_guard<std::mutex> lock (state.mutex);
    if (controller == nullptr || state.refreshController != nullptr) return 0;
    state.refreshController = controller;
    return ++state.refreshIssued;
}

bool consumeReferenceTestWorkspaceRefresh (const void* controller) noexcept
{
    auto& state = commits();
    {
        const std::lock_guard<std::mutex> lock (state.mutex);
        if (state.refreshController != controller) return false;
        state.refreshController = nullptr;
        state.consumedController = controller;
        state.refreshConsumed = state.refreshIssued;
        ++state.sequence;
    }
    state.changed.notify_all();
    return true;
}

bool referenceTestWorkspaceRefreshConsumed (std::uint64_t ticket) noexcept
{
    auto& state = commits();
    const std::lock_guard<std::mutex> lock (state.mutex);
    return ticket != 0 && state.refreshConsumed == ticket;
}

void cancelReferenceTestWorkspaceRefresh (const void* controller, std::uint64_t ticket) noexcept
{
    auto& state = commits();
    const std::lock_guard<std::mutex> lock (state.mutex);
    if (state.refreshController == controller && (ticket == 0 || state.refreshIssued == ticket))
        state.refreshController = nullptr;
    if (state.consumedController == controller && (ticket == 0 || state.refreshConsumed == ticket))
    { state.consumedController = nullptr; state.refreshConsumed = 0; }
}
}

namespace hypha::tests::reference_control
{
std::uint64_t commitSequence()
{
    auto& state = commits();
    const std::lock_guard<std::mutex> lock (state.mutex);
    return state.sequence;
}
bool waitForCommitAfter (std::uint64_t sequence, Deadline deadline)
{
    auto& state = commits();
    std::unique_lock<std::mutex> lock (state.mutex);
    return state.changed.wait_until (lock, deadline, [&] { return state.sequence != sequence; });
}

void verifyCommitWaitContract()
{
    const auto check = [] (bool ok, const char* reason) {
        if (! ok) { std::cerr << "Reference commit wait: " << reason << '\n'; std::exit (1); }
    };
    int calls = 0;
    check (! waitUntil (std::chrono::steady_clock::now() - std::chrono::seconds (1), [&] { ++calls; return true; })
               && calls == 0, "an expired deadline never runs the predicate");
    auto current = Deadline {};
    const auto end = current + std::chrono::seconds (1);
    check (! waitUntil (end, [&] { ++calls; current = end; return true; }, [&] { return current; })
               && calls == 1, "a predicate completing at the deadline cannot succeed");
    check (! waitForCommitAfter (commitSequence(), std::chrono::steady_clock::now() + std::chrono::milliseconds (20)),
           "the real condition variable times out without a notification");
    const auto past = std::chrono::steady_clock::now() - std::chrono::seconds (1);
    const auto beforePast = [&] { return past - std::chrono::seconds (1); };
    calls = 0;
    check (! waitUntil (past, [&] { ++calls; return false; }, beforePast)
               && calls == 1, "a condition-variable timeout never reruns the predicate");
    calls = 0;
    check (waitUntil (past, [&] {
        if (++calls == 1) { reference_audition::notifyReferenceTestCommit(); return false; }
        return true;
    }, beforePast) && calls == 2, "a commit between sequence reading and waiting is not lost");
    std::cout << "Reference commit wait deadline and notification boundaries PASS\n";
}
}

bool prepareReferenceRuntimeInvocation (int argc, char** argv)
{
    std::cout << "NATIVE_CONFIG"
#ifdef JUCE_DEBUG
              << " JUCE_DEBUG=" << JUCE_DEBUG
#else
              << " JUCE_DEBUG=undefined"
#endif
#ifdef _DEBUG
              << " _DEBUG=1"
#else
              << " _DEBUG=0"
#endif
#ifdef NDEBUG
              << " NDEBUG=1"
#else
              << " NDEBUG=0"
#endif
              << std::endl;
    hypha::tests::reference_control::verifyCommitWaitContract();
    if (argc == 2 && juce::String (argv[1]) == "--commit-wait-only") return true;
    verifyABindingReadClockContract();
    verifyCaptureServiceClockContract();
    return argc == 2 && juce::String (argv[1]) == "--binding-read-clock-only";
}
