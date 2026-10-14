#include "../src/live_compare/CallbackWallClock.h"
#include <iostream>
#include <thread>
#ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
 #error The shipping clock contract must not compile with the native-fixture seam
#endif

int main()
{
    using namespace std::chrono;
    const auto read = [] { return static_cast<std::uint64_t> (
        duration_cast<nanoseconds> (steady_clock::now().time_since_epoch()).count()); };
    const auto before = read();
    const auto first = hypha::live_compare::callbackWallNanos();
    std::this_thread::sleep_for (milliseconds (25));
    const auto last = hypha::live_compare::callbackWallNanos();
    const auto after = read();
    if (first < before || last > after || last - first < 20'000'000) return 1;
    std::cout << "shipping callback clock remains steady_clock PASS\n";
}
