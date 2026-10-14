#pragma once
#include "../src/live_compare/CallbackWallClock.h"
#include <cmath>
#include <stdexcept>

#ifndef KIRIN_HYPHA_TEST_WALL_CLOCK
 #error ProductTestClock requires the native-test processor library, never a shipping plugin
#endif

namespace hypha::tests
{
// One owner per synthetic host. Construct before starting callbacks; destroy after joining
// them. Only that host advances time. Product readers (including the service thread) load the
// same atomic. Real waits, GUI scheduling and worker handoffs do not advance this clock.
class ProductTestClock
{
public:
    ProductTestClock()
    {
        std::uint64_t expected = 0;
        if (! live_compare::testCallbackNanos.compare_exchange_strong (expected, initial))
            throw std::logic_error ("only one synthetic host may own the callback clock");
    }
    ~ProductTestClock() { live_compare::testCallbackNanos.store (0); }
    ProductTestClock (const ProductTestClock&) = delete;
    ProductTestClock& operator= (const ProductTestClock&) = delete;

    void advanceBlock (int frames, double rate)
    {
        // Carry fractional nanoseconds, including 44.1 kHz, without per-block rounding drift.
        remainder += static_cast<long double> (frames) * 1.0e9L / rate;
        const auto whole = static_cast<std::uint64_t> (std::floor (remainder));
        remainder -= whole;
        advance (std::chrono::nanoseconds (whole));
    }
    void advance (std::chrono::nanoseconds duration)
    {
        if (duration.count() < 0) throw std::logic_error ("callback clock cannot reverse");
        live_compare::testCallbackNanos.fetch_add (static_cast<std::uint64_t> (duration.count()));
    }
    std::uint64_t nanos() const { return live_compare::callbackWallNanos(); }
    std::chrono::steady_clock::time_point now() const
    { return std::chrono::steady_clock::time_point (std::chrono::nanoseconds (nanos())); }

private:
    static constexpr std::uint64_t initial = 1'000'000'000;
    long double remainder = 0;
};
}
