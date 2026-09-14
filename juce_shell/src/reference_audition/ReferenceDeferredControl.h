#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <algorithm>
#include <juce_core/juce_core.h>
namespace hypha::reference_audition
{
// One lightweight retirement clock per process, independent of source/journal I/O.
// Audio callbacks only store atomics. Registration and teardown are non-RT.
class DeferredControl final
{
    struct Entry { juce::CriticalSection mutex; std::function<void()> action; };
    class Service final : private juce::Thread
    {
    public:
        Service() : juce::Thread ("Reference control") { startThread(); }
        ~Service() override { signalThreadShouldExit(); notify(); stopThread (-1); }
        void add (const std::shared_ptr<Entry>& value)
        { const juce::ScopedLock lock (mutex); entries.push_back (value); }
        void remove (const std::shared_ptr<Entry>& value)
        { const juce::ScopedLock lock (mutex); entries.erase (std::remove (entries.begin(), entries.end(), value), entries.end()); }
    private:
        void run() override
        {
            while (!threadShouldExit())
            {
                std::vector<std::shared_ptr<Entry>> pending;
                { const juce::ScopedLock lock (mutex); pending = entries; }
                for (const auto& item : pending)
                { const juce::ScopedLock lock (item->mutex); if (item->action) item->action(); }
                wait (20);
            }
        }
        juce::CriticalSection mutex;
        std::vector<std::shared_ptr<Entry>> entries;
    };
public:
    ~DeferredControl() { stop(); }
    void start (std::function<void()> action)
    { stop(); entry = std::make_shared<Entry>(); entry->action = std::move (action); service->add (entry); }
    void stop()
    {
        if (!entry) return;
        service->remove (entry);
        { const juce::ScopedLock lock (entry->mutex); entry->action = {}; }
        entry.reset();
    }
private:
    juce::SharedResourcePointer<Service> service;
    std::shared_ptr<Entry> entry;
};
}
