#include "InitialSourcePhaseWait.h"
#include "../src/reference_audition/ReferenceTestCommitObserver.h"
#include <future>
#include <iostream>
#include <thread>
using hypha::tests::editor_product::InitialSourcePhaseWait;
int main()
{
    const auto require = [] (bool ok) { if (!ok) throw std::runtime_error ("phase notification contract"); };
    {
        InitialSourcePhaseWait phase;
        auto observed = phase.sequence();
        KIRIN_REFERENCE_COMMIT_FOR_TEST();
        require (phase.waitAfter (observed, std::chrono::steady_clock::now()));
        observed = phase.sequence();
        require (!phase.waitAfter (observed, std::chrono::steady_clock::now()));
        bool rejected = false;
        try { InitialSourcePhaseWait second; } catch (const std::runtime_error&) { rejected = true; }
        require (rejected);
        std::promise<void> waiting;
        auto ready = waiting.get_future();
        auto result = std::async (std::launch::async, [&] {
            const auto before = phase.sequence();
            waiting.set_value();
            return phase.waitAfter (before, std::chrono::steady_clock::now() + std::chrono::seconds (60));
        });
        ready.get(); InitialSourcePhaseWait::notify(); require (result.get());
    }
    require (hypha::reference_audition::productTestNotification.load() == nullptr);
    InitialSourcePhaseWait reacquired;
    std::cout << "initial source phase notification: PASS\n";
}
