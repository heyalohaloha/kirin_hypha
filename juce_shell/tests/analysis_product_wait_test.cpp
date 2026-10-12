#include "AnalysisProductWait.h"
#include <iostream>
#include <stdexcept>

namespace
{
void require (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); }
using namespace hypha::tests;
using namespace std::chrono;

void slowSchedulerKeepsTheAudioBudget()
{
    for (const auto rate : { 48000u, 96000u })
        for (const auto slowdown : { 1, 5, 6 })
        {
            AnalysisTestClock::time_point time {};
            const auto budget = analysisWaitBlocks (rate, 256);
            std::uint64_t blocks = 0;
            const auto result = waitForAnalysis (budget, [&] {
                ++blocks;
                time += milliseconds (5 * slowdown);
            }, [&] { return blocks == budget; }, [&] { return time; });
            require (result == AnalysisWaitResult::ready && blocks == budget,
                     "a slow scheduler must supply the same audio, including the final allowed block");
        }
}

void failuresStayBounded()
{
    AnalysisTestClock::time_point time {};
    std::uint64_t blocks = 0;
    auto result = waitForAnalysis (20, [&] { ++blocks; }, [] { return false; }, [&] { return time; });
    require (result == AnalysisWaitResult::audioLimit && blocks == 20,
             "a missing result must fail at the exact audio limit");
    blocks = 0;
    result = waitForAnalysis (1000, [&] { ++blocks; time += seconds (1); },
                              [] { return false; }, [&] { return time; });
    require (result == AnalysisWaitResult::wallLimit && blocks == 150,
             "the wall watchdog must stop a stalled worker without granting more audio");
    result = waitForAnalysis (1, [&] { time += seconds (150); },
                              [] { return true; }, [&] { return time; });
    require (result == AnalysisWaitResult::wallLimit,
             "a result arriving at the wall watchdog must not turn a stall into success");
    blocks = 0;
    result = waitForAnalysis (0, [&] { ++blocks; }, [] { return true; }, [&] { return time; });
    require (result == AnalysisWaitResult::audioLimit && blocks == 0, "zero budget cannot process a block");
    bool threw = false;
    try
    {
        waitForAnalysis (10, [] {}, [] () -> bool { throw std::runtime_error ("bad delta"); },
                         [&] { return time; });
    }
    catch (const std::runtime_error&) { threw = true; }
    require (threw, "assertion failures must escape immediately, never become a retry");
}

void pacingDoesNotAddWorkOrCatchUp()
{
    AnalysisTestClock::time_point time {};
    for (const auto work : { 2, 20, 2 })
    {
        const auto start = time;
        const auto deadline = analysisBlockDeadline (start);
        time += milliseconds (work);
        if (time < deadline) time = deadline;
        require (time - start == milliseconds (work < 5 ? 5 : work),
                 "each block pays only its remaining pacing interval, with no catch-up burst");
    }
    require (analysisWaitBlocks (48000, 256) == 2250, "48 kHz audio budget");
    require (analysisWaitBlocks (96000, 256) == 4500, "96 kHz gets the same input duration");
    require (analysisWaitBlocks (44100, 256) == 2068, "partial final blocks round up");
}
}

int main()
try
{
    slowSchedulerKeepsTheAudioBudget();
    failuresStayBounded();
    pacingDoesNotAddWorkOrCatchUp();
    std::cout << "Analysis wait: slow scheduler, limits, assertion propagation and pacing PASS\n";
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
