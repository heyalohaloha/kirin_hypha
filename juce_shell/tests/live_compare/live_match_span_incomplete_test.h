#pragma once

static void incompleteAllPlayedCannotMeasureUntilRestart()
{
    Pair pair;
    auto& span = pair.renderer.wholeMatch;
    span.prepare (true, rate);
    KirinLiveMatchSpanFacts facts {};
    pair.run (8.0, 0.6f, 0.6f); // Intentionally withhold reader for longer than the bounded queue.
    span.service (2);
    require (span.lost() && !span.measure (facts), "overflow invalidates ALL PLAYED rather than approving a subset");
    const auto feedAndService = [&](int n) {
        for (int i = 0; i < n; ++i) { pair.runBlocks (20, 0.6f, 0.6f); span.service (2); }
    };
    feedAndService (20);
    require (computeMatch (*pair.ring, pair.renderer, rate).ok(), "LAST 4 s is independently available");
    require (span.lost() && !span.measure (facts), "a later good four-second tail cannot recover an incomplete epoch");

    span.clear(); // END, then LISTEN creates a fresh sampler with no prior contribution.
    span.prepare (true, rate);
    feedAndService (5);
    require (!span.lost() && !span.enough() && !span.measure (facts), "fresh LISTEN must wait for three confirmed seconds");
    feedAndService (10);
    require (!span.lost() && span.enough() && span.measure (facts), "fresh LISTEN can match again");
}
