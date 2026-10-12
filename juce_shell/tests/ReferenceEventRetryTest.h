#pragma once
#include "../src/reference_audition/ReferenceRuntimeV2Controller.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests::event_retry
{
using Runtime = reference_audition::RuntimeV2Controller;
using Context = reference_audition::RuntimeEventContext;
struct Facts { juce::String startId, completeId; std::int64_t started = 0, completed = 0, retry = 0; int attempts = 0; bool written = false; };
struct LockTag {}; juce::CriticalSection& lockFor (Runtime&, LockTag);
template <auto Member> struct LockAccess
{ friend juce::CriticalSection& lockFor (Runtime& r, LockTag) { return r.*Member; } };
template struct LockAccess<&Runtime::stateLock>;
struct FactsTag {}; Facts factsFor (Runtime&, FactsTag);
template <auto Member> struct FactsAccess
{
    friend Facts factsFor (Runtime& r, FactsTag)
    {
        const juce::ScopedLock lock (lockFor (r, {}));
        const auto& sessions = r.*Member;
        if (sessions.empty()) return {};
        const auto& s = sessions.back();
        return { s.startedEventId, s.completedEventId, s.startedAtMs, s.completedAtMs, s.retryAtMs, s.retries, s.startWritten };
    }
};
template struct FactsAccess<&Runtime::auditionEventSessions>;
struct ContextTag {}; void contextFor (Runtime&, Context, ContextTag);
template <auto Member> struct ContextAccess
{
    friend void contextFor (Runtime& r, Context context, ContextTag)
    { const juce::ScopedLock lock (lockFor (r, {})); r.*Member = std::move (context); }
};
template struct ContextAccess<&Runtime::activeEventContext>;
struct BTag {}; void confirmB (Runtime&, BTag);
struct ATag {}; void confirmA (Runtime&, ATag);
// Separate definitions avoid exposing any production test hook.
template <auto Member> struct BAccess { friend void confirmB (Runtime& r, BTag) { (r.*Member).fetch_add (1); } };
template <auto Member> struct AAccess { friend void confirmA (Runtime& r, ATag) { (r.*Member).fetch_add (1); } };
template struct BAccess<&Runtime::bAudibleConfirmations>;
template struct AAccess<&Runtime::aAudibleConfirmations>;
struct BeginTag { using Type = void (Runtime::*) (std::uint64_t) noexcept; friend Type method (BeginTag); };
struct ReturnTag { using Type = void (Runtime::*) (std::uint64_t) noexcept; friend Type method (ReturnTag); };
template <class Tag, typename Tag::Type Member> struct MethodAccess { friend typename Tag::Type method (Tag) { return Member; } };
template struct MethodAccess<BeginTag, &Runtime::beginAuditionEventSession>;
template struct MethodAccess<ReturnTag, &Runtime::requestAuditionReturnEvent>;
inline void check (bool ok, const char* why)
{ if (! ok) { std::cerr << "Reference event retry: " << why << '\n'; std::exit (1); } }
inline void verify (const juce::File& fixture, const Context& context)
{
    const auto root = fixture.getChildFile ("immutable-event-retry");
    const auto events = root.getChildFile (context.identity.library ? "library/events" : "events");
    check (events.getParentDirectory().createDirectory() && events.replaceWithText ("blocked fixture"), "block fixture event directory");
    Runtime runtime (root);
    contextFor (runtime, context, {});
    (runtime.*method (BeginTag {})) (0); confirmB (runtime, {});
    const auto wait = [&] (const auto& predicate) {
        for (int i = 0; i < 700; ++i) { const auto facts = factsFor (runtime, {}); if (predicate (facts)) return facts; juce::Thread::sleep (10); }
        check (false, "worker retry fixture deadline"); return Facts(); };
    const auto first = wait ([] (const Facts& f) { return f.attempts == 1; });
    check (first.started > 0 && first.retry >= first.started + 1000, "first failure reserves an interval");
    juce::Thread::sleep (200);
    check (factsFor (runtime, {}).attempts == 1, "frequent worker service cannot repeat failed I/O");
    const auto second = wait ([] (const Facts& f) { return f.attempts == 2; });
    check (second.started == first.started && second.retry >= first.retry + 2000,
           "retry preserves the exact event timestamp and doubles the interval");
    check (events.deleteFile() && events.createDirectory(), "recover only the fixture sink");
    const auto written = wait ([] (const Facts& f) { return f.written; });
    reference_audition::RuntimeEventTransport transport (root);
    const auto bytes = transport.eventFile (context.identity.runtimeInstanceId, written.startId).loadFileAsString();
    check (bytes.isNotEmpty(), "recovered sink stores the original start event");
    const auto completion = transport.eventFile (context.identity.runtimeInstanceId, written.completeId);
    check (completion.createDirectory(), "block only the fixture completion destination");
    (runtime.*method (ReturnTag {})) (0); confirmA (runtime, {});
    const auto completionFirst = wait ([] (const Facts& f) { return f.attempts == 1 && f.completed > 0; });
    juce::Thread::sleep (200);
    check (factsFor (runtime, {}).attempts == 1, "completion I/O failure cannot repeat every worker tick");
    const auto completionSecond = wait ([] (const Facts& f) { return f.attempts == 2; });
    check (completionSecond.completed == completionFirst.completed
        && completionSecond.retry >= completionFirst.retry + 2000, "completion timestamp and retry interval are stable");
    check (completion.deleteRecursively(), "remove only the empty fixture blocker");
    wait ([] (const Facts& f) { return f.startId.isEmpty(); });
    check (transport.eventFile (context.identity.runtimeInstanceId, written.startId).loadFileAsString() == bytes,
           "completion never changes the immutable start bytes");
}
}
