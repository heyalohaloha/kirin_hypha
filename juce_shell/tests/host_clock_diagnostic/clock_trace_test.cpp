#include "ClockTrace.h"
#include <cassert>
#include <thread>

int main()
{
    using namespace hypha::clock_diagnostic;
    ClockTrace<3> bounded;
    Row row;
    bounded.append (row);
    assert (bounded.size() == 0);
    bounded.start();
    for (int i = 0; i < 8; ++i) { row.project = i; bounded.append (row); }
    assert (bounded.size() == 3 && bounded[0].project == 0 && bounded[2].project == 2);
    bounded.start(); // Never resets/overwrites an exported prefix.
    bounded.append (row);
    assert (bounded.size() == 3 && bounded[0].project == 0);
    ClockTrace<4096> concurrent;
    concurrent.start();
    std::thread writer ([&]
    {
        for (int i = 0; i < 4096; ++i)
        {
            Row next;
            next.project = i;
            next.auxiliary = -i;
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
            assert (concurrent[checked].project == static_cast<std::int64_t> (checked));
            assert (concurrent[checked].auxiliary == -static_cast<std::int64_t> (checked));
            assert (concurrent[checked].frames == checked + 1);
        }
    }
    writer.join();
}
