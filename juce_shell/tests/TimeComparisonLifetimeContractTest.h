#pragma once
#include "../src/HyphaTimeSnapshotPresentation.h"
#include <cstdlib>
#include <iostream>

inline void verifyTimeComparisonLifetimeContract (
    KirinTimeSnapshotV2 packet, const std::vector<KirinTimeHistoryEntryV2>& mainHistory,
    const std::vector<KirinTimeHistoryEntryV2>& psrHistory)
{
    auto require = [] (bool condition)
    {
        if (! condition)
        {
            std::cerr << "TIME comparison one-second lifetime contract failed\n";
            std::exit (EXIT_FAILURE);
        }
    };
    hypha::time_snapshot::Presentation accepted;
    packet.main.current.completion_age_ms = 0.0;
    packet.main.current.remaining_ms = 400.0;
    packet.psr.current.completion_age_ms = 0.0;
    packet.psr.current.remaining_ms = 1000.0;
    require (accepted.apply (packet, mainHistory, psrHistory, 0, 0, true));
    for (const double age : { 401.0, 999.0 })
    {
        packet.psr.current.completion_age_ms = age;
        packet.psr.current.remaining_ms = 1000.0 - age;
        require (accepted.apply (packet, mainHistory, psrHistory, age, age, true));
        require (accepted.psr().currentAvailable (hypha::time_snapshot::Metric::psr));
        require (accepted.psr().deadlineMs == 1000.0);
        require (! accepted.main().currentAvailable (hypha::time_snapshot::Metric::momentary));
    }
    require (accepted.advance (1000.0));
    require (! accepted.psr().currentAvailable (hypha::time_snapshot::Metric::psr));
    // Absolute data still cannot claim a one-second lease.
    packet.main.current.remaining_ms = 1000.0;
    require (! accepted.apply (packet, mainHistory, psrHistory, 1000, 1000, true));
    packet.main.current.remaining_ms = 400.0;
    packet.psr.current.remaining_ms = 1001.0;
    require (! accepted.apply (packet, mainHistory, psrHistory, 1000, 1000, true));
}
