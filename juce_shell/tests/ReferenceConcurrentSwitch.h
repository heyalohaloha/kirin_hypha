#pragma once
// B と C を callback の合間に切り替える試験と、別の thread から並行に切り替える試験の補助
// （testReferenceComparisons）。完了の知らせは library の events に Kirin OS が読む形で残る。
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <thread>

namespace hypha::tests
{
inline int referenceAuditionCompletions (const juce::File& root)
{
    int count = 0;
    for (const auto& file : root.getChildFile ("library/events").findChildFiles (juce::File::findFiles, true, "*.json"))
        if (juce::JSON::parse (file)["event_type"].toString() == "audition_completed") ++count;
    return count;
}

struct ConcurrentSwitching { int actualABlocks = 0, refusedSelections = 0; };

// A second thread switches C, B, C, ... (100 commands, ending with B) while 200 callbacks run.
// `block` returns false when the callback output A; `checkBlock` checks each callback's samples.
inline ConcurrentSwitching switchConcurrently (const std::function<bool()>& selectC,
                                               const std::function<bool()>& selectB,
                                               const std::function<bool()>& block,
                                               const std::function<void()>& checkBlock)
{
    std::atomic<int> refused { 0 };
    std::thread switches ([&] {
        for (int i = 0; i < 100; ++i)
        {
            if (! ((i & 1) == 0 ? selectC() : selectB())) refused.fetch_add (1);
            juce::Thread::sleep (1);
        }
    });
    ConcurrentSwitching result;
    for (int i = 0; i < 200; ++i)
    {
        if (! block()) ++result.actualABlocks;
        checkBlock();
        juce::Thread::sleep (1);
    }
    switches.join();
    result.refusedSelections = refused.load();
    return result;
}
}
