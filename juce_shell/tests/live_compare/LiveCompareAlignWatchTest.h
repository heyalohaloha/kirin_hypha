#pragma once
#include "../../src/live_compare/LiveCompareAlign.h"

// Deterministic owner/audio interleavings. No scheduler or wall-clock assumptions.
inline void alignWatchRaces()
{
    using hypha::live_compare::AlignWatch;
    {
        AlignWatch w; w.watchKept (1000); w.record (w.snapshot().kept);
        require (w.takenOver() == 1000 && w.takenOver() == 0, "watch receipt consumed once");
    }
    {
        AlignWatch w; w.watchKept (1000); w.watchNext (2000);
        w.record (w.snapshot().next); w.watchNext (0);
        require (w.takenOver() == 0, "rejected next never drops kept");
    }
    {
        AlignWatch w; w.watchKept (1000); w.watchNext (2000);
        const auto delayed = w.snapshot().next; w.watchKept (2000);
        require (w.takenOver() == 0, "promotion alone is not a receipt");
        w.watchNext (3000); w.record (delayed); w.record (w.snapshot().next); w.watchNext (0);
        require (w.takenOver() == 2000, "promoted delayed receipt survives rejected later match");
    }
    for (bool delayed : { false, true }) for (int count : { 8193, 100000, 1000000 })
    {
        AlignWatch w; w.watchKept (1000); const auto seen = w.snapshot().kept;
        if (!delayed) w.record (seen);
        for (int i = 0; i < count; ++i) { w.watchNext (2000); w.watchNext (0); }
        if (delayed) w.record (seen);
        require (w.takenOver() == 1000, "kept identity survives unrelated publications without epoch wrap");
    }
    for (bool legitimate : { false, true })
    {
        AlignWatch w; w.watchKept (1000); w.watchNext (2000);
        const auto rejected = w.snapshot().next; w.watchNext (0); w.watchNext (3000);
        const auto next = w.snapshot().next;
        if (legitimate) w.record (next);
        w.record (rejected); w.watchKept (3000);
        require (w.takenOver() == (legitimate ? 3000 : 0), "stale receipt neither creates nor destroys new identity's hit");
    }
    {
        static AlignWatch* interrupted = nullptr;
        AlignWatch w; w.watchKept (1000); w.watchNext (2000); interrupted = &w;
        hypha::live_compare::betweenAlignWatchLoadsForTest = [] { interrupted->watchKept (2000); };
        const auto seen = w.snapshot();
        hypha::live_compare::betweenAlignWatchLoadsForTest = nullptr; interrupted = nullptr;
        if (AlignWatch::frames (seen.next) == 2000) w.record (seen.next);
        if (AlignWatch::frames (seen.kept) == 2000) w.record (seen.kept);
        require (w.takenOver() == 2000 && w.takenOver() == 0, "promotion between loads keeps a single identifiable receipt");
    }
    {
        using namespace hypha::live_compare;
        static const AlignReadiness* interrupted = nullptr;
        static std::int64_t consumed = 0;
        AlignReadiness readiness;
        readiness.observe (true, true, true, 0);
        readiness.watchKept (1000, 2); readiness.watchNext (2000, 2); interrupted = &readiness;
        betweenAlignWatchLoadsForTest = [] { interrupted->watchKept (2000, 2); };
        afterAlignWatchKeptForTest = [] (const AlignReadiness& clock) { consumed = clock.takenOver(); };
        readiness.observe (true, true, true, 2000);
        betweenAlignWatchLoadsForTest = nullptr; afterAlignWatchKeptForTest = nullptr; interrupted = nullptr;
        require (consumed == 2000 && readiness.takenOver() == 0,
                 "one K step is not re-registered when owner consumes between identical snapshot words");
    }

}
