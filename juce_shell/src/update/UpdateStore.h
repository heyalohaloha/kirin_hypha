#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <memory>

namespace hypha::update
{
// Worker-only, dedicated update storage. Save/transactional read require the OS
// lock. observe() reads one securely opened atomic snapshot, never writes/reserves.
class Store
{
public:
    struct State
    {
        bool enabled = false;
        std::int64_t lastAttempt = 0;
        std::int64_t lastSuccess = 0;
        std::int64_t lastSequence = 0;
        juce::String wire, dismissedVersion;
    };

    explicit Store (juce::File root);
    ~Store();
    Store (const Store&) = delete;
    Store& operator= (const Store&) = delete;
    bool read (State&);
    bool observe (State&);
    bool save (const State&);
    bool acquire();
    void release();
    juce::File root() const;

private:
    struct Lock;
    bool readState (Lock&, State&);
    juce::File directory;
    std::unique_ptr<Lock> heldLock;
};
}
