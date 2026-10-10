# LEVEL / TIME comparison freshness and FREQ status

2026-10-10 user decision: an already matched LEVEL/TIME point remains Active for less
than one second during continuous playback in the same exact pair, source spans and run.
This replaces the comparison-only 400 ms display lease. Absolute current still expires
after 400 ms. The measured apertures remain 400 ms / 3 s.

The deadline starts at the original local POST slot completion. A poll, IO service or
rejoin cannot renew it. Both wall-clock age and local observed-frame lag must be less
than one second. The local clock kind and endpoint progression must remain continuous.
Seek, loop wrap, run/source/pair/authority/layout change, stop/pause and known missing
metrics invalidate the affected current immediately. IO contention yields no new packet.
LEVEL checks the local Measure facts before using an IO-owned matched point, as TIME does.
Neither path changes audio processing, Record serialization or the public C ABI.

FREQ uses the spectrum's own availability and freshness. LEVEL AwaitingMeasurement,
Stale and LocalInactive do not replace its delta legend or MARK. NoPair, PreBypassed,
PreInactive, LayoutMismatch, LayoutUnknown, AuditionActive and unsupported comparisons
retain the existing status row. A stale or unavailable spectrum still follows its own
existing display rules; this change does not extend a spectrum lease.

## Publication cadence investigation

The supplied diagnosis contains matched points exceeding the former 400 ms lease while
pair, epoch and generation remain unchanged. It establishes the display failure but
does not isolate the duration of each PRE and POST IO operation.

PRE and POST use nominal 100 ms IO loops. PRE history publication is already
revision-driven; it does not rebuild unchanged history. POST previously waited a full
100 ms after observation, Record/control service and other IO work. That work therefore
added directly to the next join interval. The POST wait now subtracts work time from its
100 ms cycle budget. Work lasting at least 100 ms adds no further wait, with no catch-up
ticks or concurrent worker. This removes an avoidable contribution; slow filesystem or
PRE publication work can still delay a point. No RT work, serialization, history length,
measurement cadence or Record drain/Stop rule moves between threads.

Synthetic cadence tests cover zero, partial and over-budget processing. Local tests
cover every age from 401 to 999 ms, 1000 ms expiry, continuity and authority edges,
busy/seek acquisition, UI deadlines and pair-only FREQ rendering in both languages.
Actual REAPER cadence and reproduction acceptance remain assigned to the reviewer.
