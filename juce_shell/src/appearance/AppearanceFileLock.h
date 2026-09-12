#pragma once

#include <juce_core/juce_core.h>

namespace hypha::appearance
{
class FileLock final
{
public:
    explicit FileLock (juce::File lockDirectory);
    ~FileLock();

    bool tryAcquire();
    bool isHeld() const noexcept { return held; }

private:
    juce::File directory;
    bool held = false;

    JUCE_DECLARE_NON_COPYABLE (FileLock)
};
}
