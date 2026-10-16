#include "ReferenceControllerTestAccess.h"
#include "../src/reference_audition/ReferenceTestCommitObserver.h"
#include "ReferenceABindingReadClockTests.h"
#include "ReferenceCaptureServiceClockTests.h"
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <cstdlib>
#include <unordered_map>

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
    const void* refreshController = nullptr;
    const void* consumedController = nullptr;
    std::uint64_t refreshIssued = 0, refreshConsumed = 0;
    std::unordered_map<const void*, hypha::reference_audition::ReferenceTestCaptureResetFacts> captureResets;
};
// require() exits without destroying local controllers. Keep test notifications
// alive for the process lifetime, including a failing test's worker shutdown.
Commits& commits() { static auto* state = new Commits(); return *state; }
}

namespace hypha::reference_audition
{
void notifyReferenceTestCaptureReset (const void* capture, const char* reason, std::int64_t atMs) noexcept
{
    auto& state = commits();
    const std::lock_guard<std::mutex> lock (state.mutex);
    auto& facts = state.captureResets[capture];
    facts.reason = reason; facts.atMs = atMs; ++facts.count;
}

ReferenceTestCaptureResetFacts referenceTestCaptureResetFacts (const void* capture) noexcept
{
    auto& state = commits();
    const std::lock_guard<std::mutex> lock (state.mutex);
    const auto found = state.captureResets.find (capture);
    return found == state.captureResets.end() ? ReferenceTestCaptureResetFacts {} : found->second;
}

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
    hypha::tests::reference_control::verifyCommitWaitContract();
    if (argc == 2 && juce::String (argv[1]) == "--commit-wait-only") return true;
    verifyABindingReadClockContract();
    verifyCaptureServiceClockContract();
    return argc == 2 && juce::String (argv[1]) == "--binding-read-clock-only";
}
