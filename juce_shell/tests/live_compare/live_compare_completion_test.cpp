#include "../../src/live_compare/LiveCompareCompletion.h"
#include "../../src/live_compare/LiveCompareSession.h"
#include <cstdlib>
#include <iostream>

static void require (bool value, const char* message)
{
    if (! value) { std::cerr << message << '\n'; std::exit (1); }
}

int main()
{
    using namespace hypha::live_compare;
    Completion finish;
    PostLevel level;
    level.configure (48000);
    for (int i = 0; i < 2400; ++i) level.next (0.5f);
    const auto request = finish.request();
    require (finish.pending(), "END is a request, not a receipt");
    require (finish.request() == request, "repeated END is idempotent");
    finish.observe (request, false, false, 1.0f);
    require (finish.pending(), "offline/bypass cannot complete a realtime return");
    finish.observe (request, true, true, 1.0f);
    require (finish.pending(), "PRE must be completely gone");
    int frames = 0;
    while (level.value() < 1.0f && frames < 100000)
    {
        const float previous = level.value();
        level.next (1.0f);
        require (level.value() >= previous && level.value() - previous < 0.000045f,
                 "POST rises monotonically at the half-second full-range slope");
        finish.observe (request, true, false, level.value());
        if (level.value() < 1.0f) require (finish.pending(), "no early success");
        ++frames;
    }
    // Float steps accumulate slightly short of the exact 12000 frames.
    require (! finish.pending() && frames >= 11900 && frames <= 12030, "-6 dB reaches unity in about 12000 frames (0.25 s)");
    const auto second = finish.request();
    finish.observe (request, true, false, 1.0f);
    require (finish.pending(), "an old completion cannot finish a new END");
    finish.observe (second, true, false, 1.0f);
    require (! finish.pending(), "unity END completes with its own RT receipt");
    // INV-LC14: while the DAW is stopped a rise takes the 5 ms transition, also across short blocks,
    // and a later rise after a move down is the half-second one again.
    for (int i = 0; i < 2400; ++i) level.next (0.5f);
    level.riseAtOnce (true);
    int quick = 0;
    for (; level.value() < 1.0f && quick < 1000; ++quick) level.next (1.0f);
    require (quick >= 119 && quick <= 121, "a stopped DAW returns -6 dB in 2.5 ms (5 ms full range)");
    for (int i = 0; i < 2400; ++i) level.next (0.5f);
    int slow = 0;
    for (; level.value() < 1.0f && slow < 100000; ++slow) level.next (1.0f);
    require (slow >= 11900, "a move down ends the quick rise");
    // No callback for half a second: nothing sounds, so the message thread may finish the return.
    require (quietSince (0, 1) && ! quietSince (1'000'000'000, 1'499'999'999) && quietSince (1'000'000'000, 1'500'000'000)
             && ! quietSince (2'000'000'000, 1'000'000'000), "quiet means half a second without a callback");
    const auto third = finish.request();
    finish.complete (second);
    require (finish.pending(), "an old token cannot finish a new END in a quiet");
    finish.complete (third);
    require (! finish.pending() && finish.receipt() == third, "a quiet finishes the current END");
    std::cout << "Live completion: PASS; return frames=" << frames << " quick=" << quick << '\n';
}
