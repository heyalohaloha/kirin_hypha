#pragma once
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#if JUCE_MAC
 #include <mach/mach.h>
 #include <mach/mach_time.h>
 #include <mach/thread_policy.h>
 #include <pthread.h>
#endif

namespace hypha::test
{
// Test thread only. QoS alone is not Mach's audio time-constraint scheduling band. This explicit
// diagnostic changes neither product threads, DAWs, devices nor task/global scheduling. Verify
// the actual policy before and after each measured window; demotion is a failure, not a pass.
class BenchmarkRealtimePolicy
{
public:
    void configure (int frames)
    {
        const auto* input = std::getenv ("KIRIN_BENCHMARK_REALTIME");
        if (input == nullptr) return;
        check (std::string (input) == "1", "explicit realtime fixture input must be 1");
       #if JUCE_MAC
        mach_timebase_info_data_t timebase {};
        check (mach_timebase_info (&timebase) == KERN_SUCCESS && timebase.numer != 0,
               "Mach timebase is available");
        const auto nanos = static_cast<std::uint64_t> (frames) * 1'000'000'000 / 48000;
        wanted.period = static_cast<std::uint32_t> (nanos * timebase.denom / timebase.numer);
        wanted.computation = wanted.period / 2;
        wanted.constraint = wanted.period;
        wanted.preemptible = TRUE;
        check (thread_policy_set (pthread_mach_thread_np (pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
            reinterpret_cast<thread_policy_t> (&wanted), THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS,
            "fixture-only time constraint policy is accepted");
        enabled = true;
        verify();
        std::cout << "benchmark_realtime=1 period_ticks=" << wanted.period
                  << " computation_ticks=" << wanted.computation
                  << " constraint_ticks=" << wanted.constraint << " preemptible=1 verified=1\n";
       #else
        (void) frames;
        check (false, "time-constraint fixture is macOS-only");
       #endif
    }

    void verify() const
    {
        if (! enabled) return;
       #if JUCE_MAC
        thread_time_constraint_policy_data_t actual {};
        auto count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
        boolean_t defaults = FALSE;
        check (thread_policy_get (pthread_mach_thread_np (pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
            reinterpret_cast<thread_policy_t> (&actual), &count, &defaults) == KERN_SUCCESS
            && ! defaults && count == THREAD_TIME_CONSTRAINT_POLICY_COUNT
            && actual.period == wanted.period && actual.computation == wanted.computation
            && actual.constraint == wanted.constraint && actual.preemptible == TRUE,
            "actual fixture time-constraint policy remains active");
       #endif
    }
private:
    static void check (bool ok, const char* message)
    {
        if (! ok) { std::cerr << "Benchmark realtime policy: " << message << '\n'; std::exit (EXIT_FAILURE); }
    }
    bool enabled = false;
   #if JUCE_MAC
    thread_time_constraint_policy_data_t wanted {};
   #endif
};
}
