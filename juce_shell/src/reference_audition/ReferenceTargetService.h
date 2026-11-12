#pragma once
#include "ReferenceTargetMeasurement.h"
#include "ReferenceTargetTypes.h"
#include "ReferenceTestCommitObserver.h"
#include <atomic>
#include <optional>

namespace hypha::reference_audition
{
// A full collection never drops a target on its own: it says how many were not added and how to make
// room (HyphaJapaneseReference.cpp).
inline juce::String targetsFullIssue (int notAdded)
{
    return juce::String (notAdded) + " NOT ADDED: " + juce::String (static_cast<int> (TargetCollection::capacity))
        + " TARGETS MAX / REMOVE ONE TO MAKE ROOM";
}
// Only project facts are shared. The worker owns each decoder and its short-lived PCM buffers.
class TargetService final : private juce::Thread
{
public:
    using Clock = std::function<double()>;
    explicit TargetService (std::function<void()> changed = {}, std::function<void(bool)> selected = {},
                            Clock clock = juce::Time::getMillisecondCounterHiRes)
        : juce::Thread ("Reference target"), stateChanged (std::move (changed)), selectionChanged (std::move (selected)),
          nowMs (std::move (clock)) { startThread (juce::Thread::Priority::low); }
    ~TargetService() override { signalThreadShouldExit(); ++generation; notify(); stopThread (-1); }
    TargetSnapshot snapshot() const
    {
        const juce::ScopedLock guard (mutex);
        auto result = state;
        if (nowMs() >= issueUntilMs) result.issue.clear();
        return result;
    }
    int cueBlocks() const
    {
        const juce::ScopedLock guard (mutex);
        const auto* target = state.collection.find (state.collection.selected);
        return target ? static_cast<int> (std::clamp (std::round (target->duration * 10.0), 30.0, 6000.0)) : 0;
    }
    bool waitUntilIdle (int timeoutMs) { return settled.wait (timeoutMs); }
   #if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    void setBeforePublishForTest (std::function<void()> probe) { const juce::ScopedLock guard (mutex); beforePublish = std::move (probe); }
   #endif
    void restore (TargetCollection value)
    {
        const juce::ScopedLock guard (mutex);
        const auto epoch = state.restoreEpoch + 1;
        ++generation; pending.reset(); state = {}; state.restoreEpoch = epoch; state.collection = std::move (value);
        publishSelection();
        if (! workerActive) settled.signal();
        notify();
    }
    bool importFiles (const juce::StringArray& files)
    {
        const juce::ScopedLock guard (mutex);
        if (state.measuring) { setIssue ("MEASURING"); return false; }
        if (files.isEmpty()) return false;
        if (! isThreadRunning() && ! startThread (juce::Thread::Priority::low)) { setIssue ("MEASUREMENT UNAVAILABLE"); return false; }
        const auto slots = TargetCollection::capacity - state.collection.items.size();
        if (slots == 0) { setIssue (targetsFullIssue (files.size())); return false; }
        settled.reset(); state.measuring = true; state.progress = 0.0; state.issue.clear();
        pending = Job { ++generation, files, slots };
        notify(); return true;
    }
    void cancel()
    {
        const juce::ScopedLock guard (mutex);
        ++generation; pending.reset(); state.measuring = false; state.progress = 0; state.issue.clear();
        if (! workerActive) settled.signal();
        notify();
    }
    bool add (MeasuredTarget target, std::uint64_t restoreEpoch = 0)
    {
        const juce::ScopedLock guard (mutex);
        if (restoreEpoch != 0 && restoreEpoch != state.restoreEpoch) return false;
        if (state.collection.items.size() >= TargetCollection::capacity) { setIssue (targetsFullIssue (1)); return false; }
        if (state.measuring) { setIssue ("MEASURING"); return false; }
        if (! target.valid()) { setIssue ("A WINDOW UNAVAILABLE"); return false; }
        state.collection.items.push_back (std::move (target)); ++state.revision; state.issue.clear(); return true;
    }
    bool select (const juce::String& id, std::uint64_t restoreEpoch = 0)
    {
        const juce::ScopedLock guard (mutex);
        if (restoreEpoch != 0 && restoreEpoch != state.restoreEpoch) return false;
        if (id.isNotEmpty() && ! state.collection.find (id)) return false;
        state.collection.selected = id; state.issue.clear(); publishSelection(); ++state.revision; return true;
    }
    bool remove (const juce::String& id)
    {
        const juce::ScopedLock guard (mutex);
        if (! state.collection.find (id)) return false;
        auto& values = state.collection.items;
        values.erase (std::remove_if (values.begin(), values.end(), [&] (const auto& item) { return item.id == id; }), values.end());
        if (state.collection.selected == id) state.collection.selected.clear();
        publishSelection();
        ++state.revision; state.issue.clear(); return true;
    }
    void report (juce::String issue) { const juce::ScopedLock guard (mutex); setIssue (std::move (issue)); }

private:
    struct Job { std::uint64_t generation; juce::StringArray files; size_t slots; };
    mutable juce::CriticalSection mutex;
    TargetSnapshot state;
    std::optional<Job> pending;
    bool workerActive = false;
    std::atomic<std::uint64_t> generation { 0 };
    juce::WaitableEvent settled { true };
    const std::function<void()> stateChanged;
    const std::function<void(bool)> selectionChanged; // Atomic control publication only; no UI or Runtime locks under mutex.
    const Clock nowMs;
    double issueUntilMs = 0.0;
    void setIssue (juce::String issue) { state.issue = std::move (issue); issueUntilMs = nowMs() + 5000.0; }
    void publishSelection() { if (selectionChanged) selectionChanged (state.collection.selected.isNotEmpty()); }
   #if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    std::function<void()> beforePublish;
   #endif
    void run() override
    {
        while (! threadShouldExit())
        {
            std::optional<Job> job;
           #if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
            std::function<void()> probe;
           #endif
            { const juce::ScopedLock guard (mutex); job.swap (pending); workerActive = job.has_value();
             #if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
              probe = beforePublish;
             #endif
            }
            if (! job) { wait (-1); continue; }
            std::vector<MeasuredTarget> completed;
            juce::String issue;
            int notAdded = 0;  // files past the free slots, in the order they were dropped
            try
            {
                for (int i = 0; i < job->files.size(); ++i)
                {
                    if (completed.size() == job->slots)
                    { notAdded = job->files.size() - i; break; }
                    const auto cancelled = [&] { return threadShouldExit() || generation.load() != job->generation; };
                    auto result = measureTargetFile (juce::File (job->files[i]), cancelled, [&] (double progress) {
                        { const juce::ScopedLock guard (mutex);
                          if (! cancelled()) state.progress = (i + progress) / job->files.size(); }
                        KIRIN_REFERENCE_COMMIT_FOR_TEST();
                    });
                    if (cancelled()) break;
                    if (result.target.valid()) completed.push_back (std::move (result.target));
                    else if (issue.isEmpty()) issue = result.issue;
                }
            }
            catch (...) { issue = "MEASUREMENT UNAVAILABLE"; }
           #if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
            if (probe) probe();
           #endif
            bool changed = false;
            {
                const juce::ScopedLock guard (mutex);
                if (generation.load() == job->generation)
                {
                    for (auto& target : completed)
                        if (state.collection.items.size() < TargetCollection::capacity)
                        { state.collection.items.push_back (std::move (target)); changed = true; }
                        else ++notAdded;
                    ++state.revision;
                    // How many did not fit comes first; a file that could not be measured still says why.
                    if (notAdded > 0) issue = targetsFullIssue (notAdded) + (issue.isNotEmpty() ? " / " + issue : juce::String());
                    setIssue (issue); state.measuring = false; state.progress = 0.0;
                }
                workerActive = false;
                if (! pending) settled.signal();
            }
            if (changed && stateChanged) stateChanged();
            KIRIN_REFERENCE_COMMIT_FOR_TEST();
        }
    }
};
}
