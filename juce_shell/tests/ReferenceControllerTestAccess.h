#pragma once
#include "../src/reference_audition/ReferenceComparisonController.h"
#include <vector>
#include <chrono>

// Test-only read access and a control-thread publication barrier; never fabricates readiness.
namespace hypha::tests::reference_control
{
using Comparison = reference_audition::ReferenceComparisonController;
using Runtime = reference_audition::RuntimeV2Controller;
struct RoleTag { int slot; };
Runtime& roleFor (Comparison&, RoleTag);
template <auto Version, auto Check, auto Reference> struct RolesAccess
{
    friend Runtime& roleFor (Comparison& c, RoleTag tag)
    { return tag.slot == 1 ? c.*Version : tag.slot == 3 ? c.*Reference : c.*Check; }
};
template struct RolesAccess<&Comparison::version, &Comparison::check, &Comparison::reference>;
struct LockTag {};
juce::CriticalSection& lockFor (Runtime&, LockTag);
template <auto Member> struct LockAccess
{ friend juce::CriticalSection& lockFor (Runtime& r, LockTag) { return r.*Member; } };
template struct LockAccess<&Runtime::stateLock>;
struct EventFact
{
    juce::String instanceId, runId, startedId, completedId;
    std::int64_t startedAt = 0, completedAt = 0;
    bool startWritten = false, returnRequested = false;
};
struct EventsTag {};
std::vector<EventFact> eventsFor (Runtime&, EventsTag);
template <auto Member> struct EventsAccess
{
    friend std::vector<EventFact> eventsFor (Runtime& r, EventsTag)
    {
        const juce::ScopedLock lock (lockFor (r, {}));
        std::vector<EventFact> result;
        for (const auto& s : r.*Member)
            result.push_back ({ s.context.identity.runtimeInstanceId, s.runId, s.startedEventId,
                                s.completedEventId, s.startedAtMs, s.completedAtMs,
                                s.startWritten, s.returnRequested });
        return result;
    }
};
template struct EventsAccess<&Runtime::auditionEventSessions>;
struct CaptureTag {};
const reference_audition::RuntimeACapture& captureFor (const Runtime&, CaptureTag);
template <auto Member> struct CaptureAccess
{ friend const reference_audition::RuntimeACapture& captureFor (const Runtime& r, CaptureTag) { return r.*Member; } };
template struct CaptureAccess<&Runtime::aCapture>;
struct CaptureEnabledTag {};
bool captureEnabledFor (const reference_audition::RuntimeACapture&, CaptureEnabledTag);
template <auto Member> struct CaptureEnabledAccess
{ friend bool captureEnabledFor (const reference_audition::RuntimeACapture& c, CaptureEnabledTag) { return (c.*Member).load (std::memory_order_acquire); } };
template struct CaptureEnabledAccess<&reference_audition::RuntimeACapture::captureEnabled>;
struct BlindPublicationFacts
{
    reference_audition::Snapshot snapshot;
    std::uint64_t epoch = 0;
    bool ready = false;
    reference_audition::ReferenceSessionIdentity session;
};
struct BlindFactsTag {};
BlindPublicationFacts publicationFactsFor (Runtime&, BlindFactsTag);
template <auto Epoch, auto Ready, auto Blind> struct BlindFactsAccess
{
    friend BlindPublicationFacts publicationFactsFor (Runtime& r, BlindFactsTag)
    {
        const juce::ScopedLock lock (lockFor (r, {}));
        return { r.snapshot(), (r.*Epoch).load (std::memory_order_acquire),
                 (r.*Ready).load (std::memory_order_acquire), (r.*Blind).activeSessionIdentity() };
    }
};
template struct BlindFactsAccess<&Runtime::auditionEpoch, &Runtime::ready, &Runtime::blind>;
}

namespace hypha::tests::reference_control
{
using Deadline = std::chrono::steady_clock::time_point;
std::uint64_t commitSequence();
bool waitForCommitAfter (std::uint64_t, Deadline);
// Read the notification number BEFORE the predicate. No product lock is held
// while waiting, and no observer mutex is held while reading product state.
template <class Predicate, class Now> bool waitUntil (Deadline deadline, Predicate predicate, Now now)
{
    while (now() < deadline)
    {
        const auto sequence = commitSequence();
        if (now() >= deadline) return false;
        const bool ready = predicate();
        if (now() >= deadline) return false;
        if (ready) return true;
        if (! waitForCommitAfter (sequence, deadline)) return false;
    }
    return false;
}
template <class Predicate> bool waitUntil (Deadline deadline, Predicate predicate)
{ return waitUntil (deadline, predicate, [] { return std::chrono::steady_clock::now(); }); }
void verifyCommitWaitContract();
}
