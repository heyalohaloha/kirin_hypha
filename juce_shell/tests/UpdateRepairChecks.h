#pragma once
#if ! JUCE_WINDOWS
 #include <sys/stat.h>
#endif
namespace
{
void verifyUpdateRepairs (hypha::update::Configuration config, const juce::File& fixture,
                         std::atomic<juce::int64>& now, std::atomic<int>& calls)
{
    using namespace hypha::update;
    const auto clock = now.load();
    config.root = fixture.getChildFile ("future-attempt");
    Store::State future; future.enabled = true;
    future.lastAttempt = future.lastSuccess = clock + 2 * checkIntervalSeconds;
    future.lastSequence = 7;
    seed (config.root, future);
    const auto before = calls.load();
    {
        Checker checker (config);
        request (checker, true);
        require (calls == before + 1, "an extreme future stamp cannot disable checking indefinitely");
    }
    Store store (config.root); Store::State persisted;
    require (store.acquire() && store.read (persisted) && persisted.lastSequence == 7
        && persisted.lastAttempt == clock, "future repair retains the signature sequence ratchet");
    store.release();
   #if ! JUCE_WINDOWS
    config.root = fixture.getChildFile ("failed-reservation");
    Store::State state; state.enabled = true;
    seed (config.root, state);
    require (::chmod (config.root.getFullPathName().toRawUTF8(), 0500) == 0, "block only fixture state writes");
    {
        Checker checker (config);
        const auto networkBefore = calls.load();
        auto failed = request (checker, true);
        require (failed.status == Status::unavailable
            && failed.nextAttemptAt == clock + checkIntervalSeconds && calls == networkBefore,
            "a failed timestamp save schedules the next attempt in 24 hours without HTTP");
        require (::chmod (config.root.getFullPathName().toRawUTF8(), 0700) == 0, "restore fixture directory permissions");
        for (int second = 1; second <= 3; ++second)
        {
            now = clock + second;
            failed = request (checker, false);
            require (failed.nextAttemptAt == clock + checkIntervalSeconds && calls == networkBefore,
                "rereading a missing reservation cannot refund the running checker's wait");
        }
    }
    now = clock;
   #endif
}
}
