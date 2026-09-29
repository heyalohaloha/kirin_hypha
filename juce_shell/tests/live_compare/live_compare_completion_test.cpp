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
    while (level.value() < 1.0f && frames < 25000)
    {
        const float previous = level.value();
        level.next (1.0f);
        require (level.value() >= previous && level.value() - previous < 0.00005f,
                 "POST rises monotonically at the existing half-second full-range slope");
        finish.observe (request, true, false, level.value());
        if (level.value() < 1.0f) require (finish.pending(), "no early success");
        ++frames;
    }
    require (! finish.pending() && frames >= 11900 && frames <= 12100, "-6 dB reaches unity in about 12000 frames");
    const auto second = finish.request();
    finish.observe (request, true, false, 1.0f);
    require (finish.pending(), "an old completion cannot finish a new END");
    finish.observe (second, true, false, 1.0f);
    require (! finish.pending(), "unity END completes with its own RT receipt");
    std::cout << "Live completion: PASS; return frames=" << frames << '\n';
}
