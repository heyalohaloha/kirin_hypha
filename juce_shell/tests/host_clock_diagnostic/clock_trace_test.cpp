#include "ClockTrace.h"
#include <cstdio>
#include <cstdlib>
#include <thread>

static void require (bool value)
{
    if (! value) { std::fprintf (stderr, "clock trace contract failed\n"); std::abort(); }
}

int main()
{
    using namespace hypha::clock_diagnostic;
    ClockTrace<3> bounded;
    Row row;
    bounded.append (row);
    require (bounded.size() == 0);
    bounded.start();
    for (int i = 0; i < 8; ++i) { row.project = i; bounded.append (row); }
    require (bounded.size() == 3 && bounded[0].project == 0 && bounded[2].project == 2);
    bounded.start(); // Never resets/overwrites an exported prefix.
    bounded.append (row);
    require (bounded.size() == 3 && bounded[0].project == 0);
    ClockTrace<4096> concurrent;
    concurrent.start();
    std::thread writer ([&]
    {
        for (int i = 0; i < 4096; ++i)
        {
            Row next;
            next.project = i;
            next.auxiliary = -i;
            next.todSamples = i * 3;
            next.addClockSamples = i * 4;
            next.identityFrames = static_cast<std::uint32_t> (i);
            next.frames = static_cast<std::uint32_t> (i + 1);
            concurrent.append (next);
        }
    });
    std::size_t checked = 0;
    while (checked < 4096)
    {
        const auto count = concurrent.size();
        for (; checked < count; ++checked)
        {
            require (concurrent[checked].project == static_cast<std::int64_t> (checked));
            require (concurrent[checked].auxiliary == -static_cast<std::int64_t> (checked));
            require (concurrent[checked].todSamples == static_cast<std::int64_t> (checked) * 3);
            require (concurrent[checked].addClockSamples == static_cast<std::int64_t> (checked) * 4);
            require (concurrent[checked].identityFrames == checked);
            require (concurrent[checked].frames == checked + 1);
        }
    }
    writer.join();
}
