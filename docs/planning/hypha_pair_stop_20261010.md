# Exact pair binding during stop

The POST Watch tick has a dedicated non-active branch before the active exact-PRE read
and name-discovery paths. Its resolver previously used a nonempty PRE name as pair
presence. An explicitly selected unnamed PRE therefore produced Active during playback,
NoPre on stop, then Active again on resume while retaining the same instance latch.
An isolated regression using the real tick reproduced NoPre before the correction.

The non-active resolver now receives the exact PRE instance binding, which is already
part of the ordered observation snapshot. No claim release, name rescan or replacement
is required on stop. It retains the legacy last-active snapshot and minimal post.json
format, but qualifies the IO comparison with LocalInactive when a binding remains.
Active POST with a stopped PRE keeps PreInactive. A genuinely absent or explicitly
released binding remains NoPair; unresolved name intent alone is not an exact pair.
Temporary Watch publication absence remains distinct from selection retirement.

The LEVEL/observatory projection independently checks Audio inactivity or Measure
pause and the engine-owned pair authority before and after acquisition. This also
covers IO failure/restart, a delayed Active/NoPre/Stale publication, and contention on
the IO delta. A bound stopped POST exposes local stopped absolute observations and
LocalInactive without current delta facts. It does not revert to Holding on a later
IO publication. An unbound stopped POST exposes NoPair. A pair authority race yields
no new coherent frame rather than publishing a false status.

TIME current follows its own stopped state and history remains held. FREQ continues
to use its own spectrum lifecycle; LocalInactive does not become a false NoPair row,
and PreInactive is still an explicit pair reason. The accepted one-second continuous
playback lease and the complete 400 ms / 3 s apertures after resume remain unchanged.

Regression coverage includes named/unnamed exact binding, over eight seconds of
stopped Watch ticks, PRE-only stop, resume without reselection, real binding removal,
unresolved names, IO publication lag/contention, and Measure pause. The existing
optional-latency LEVEL/TIME pipeline now checks two playback runs separated by stop
and re-proves their apertures. Native rendering checks stop text in both languages;
the existing spectrum contract checks independent freshness and pair rejection.
Audio processing, Record/plugin_data serialization and HMAC, and C ABI shapes are
unchanged. Real REAPER acceptance, PR, CI, merge and release remain reviewer tasks.
