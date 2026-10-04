#pragma once

#include <juce_core/juce_core.h>

// 1 つの POST の中で、VERSION BLIND とローカル Blind（PRE/POST Blind）のどちらか 1 つだけが Blind を持つ。
// 2026-10-04：A の取り込み（capture）の部品の中にあった枠を、取り込みをやめるときにここへ移した。
namespace hypha::reference_audition
{
enum class BlindOwner { none, version, local };

class BlindSlot
{
public:
    bool reserve (BlindOwner owner)
    {
        const juce::ScopedLock lock (mutex);
        if (closed || owner == BlindOwner::none || held != BlindOwner::none) return false;
        held = owner;
        return true;
    }
    void release (BlindOwner owner)
    {
        const juce::ScopedLock lock (mutex);
        if (held == owner) held = BlindOwner::none;
    }
    BlindOwner owner() const
    {
        const juce::ScopedLock lock (mutex);
        return held;
    }
    // 片付けの後は持たせない。
    void close()
    {
        const juce::ScopedLock lock (mutex);
        closed = true;
    }

private:
    mutable juce::CriticalSection mutex;
    BlindOwner held = BlindOwner::none;
    bool closed = false;
};
}
